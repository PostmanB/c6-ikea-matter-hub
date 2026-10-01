#include "credentials.h"
#include <esp_matter_controller_credentials_issuer.h>
#include <esp_log.h>
#include <cstring>

static uint8_t fabricIpk[chip::Crypto::CHIP_CRYPTO_SYMMETRIC_KEY_LENGTH_BYTES];
static bool ipkReady = false;
void set_issuer_ipk(chip::ByteSpan ipk) {
    if (ipk.size() != sizeof(fabricIpk)) abort();
    memcpy(fabricIpk, ipk.data(), sizeof(fabricIpk));
    ipkReady = true;
}
// The upstream issuer returns a public test IPK. Replace it in the completion
// callback with exactly the persistent random key installed in our controller.
class LocalIssuer final : public chip::Controller::ExampleOperationalCredentialsIssuer {
    using Completion = chip::Callback::Callback<chip::Controller::OnNOCChainGeneration>;
    Completion *original = nullptr;
    static void completed(void *context, CHIP_ERROR status, const chip::ByteSpan &noc,
        const chip::ByteSpan &icac, const chip::ByteSpan &rcac,
        chip::Optional<chip::Crypto::IdentityProtectionKeySpan>, chip::Optional<chip::NodeId> admin) {
        auto &self = *static_cast<LocalIssuer *>(context);
        auto *callback = self.original;
        self.original = nullptr;
        chip::Crypto::IdentityProtectionKeySpan ipk(fabricIpk);
        callback->mCall(callback->mContext, status, noc, icac, rcac, chip::MakeOptional(ipk), admin);
    }
    Completion replacement{completed, this};
public:
    CHIP_ERROR GenerateNOCChain(const chip::ByteSpan &csr, const chip::ByteSpan &nonce,
        const chip::ByteSpan &signature, const chip::ByteSpan &challenge,
        const chip::ByteSpan &dac, const chip::ByteSpan &pai, Completion *callback) override {
        if (!ipkReady || original || !callback) return CHIP_ERROR_INCORRECT_STATE;
        original = callback;
        CHIP_ERROR error = ExampleOperationalCredentialsIssuer::GenerateNOCChain(
            csr, nonce, signature, challenge, dac, pai, &replacement);
        if (error != CHIP_NO_ERROR) original = nullptr;
        return error;
    }
};

// The upstream example persists its CA, but creates a fresh controller leaf
// key at boot. Retain that leaf key as well: existing ACLs identify this node.
class PersistentIssuer final : public esp_matter::controller::credentials_issuer {
    LocalIssuer issuer;
    chip::PersistentStorageDelegate *storage = nullptr;
public:
    esp_err_t initialize_credentials_issuer(chip::PersistentStorageDelegate &s) override {
        storage = &s;
        return issuer.Initialize(s) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
    }
    chip::Controller::OperationalCredentialsDelegate *get_delegate() override { return &issuer; }
    esp_err_t generate_controller_noc_chain(chip::NodeId node, chip::FabricId fabric,
        chip::Crypto::P256Keypair &key, chip::MutableByteSpan &root,
        chip::MutableByteSpan &intermediate, chip::MutableByteSpan &noc) override {
        chip::Crypto::P256SerializedKeypair bytes;
        uint16_t length = bytes.Capacity();
        CHIP_ERROR err = storage->SyncGetKeyValue("hub-leaf-key", bytes.Bytes(), length);
        if (err == CHIP_NO_ERROR) {
            if (bytes.SetLength(length) != CHIP_NO_ERROR || key.Deserialize(bytes) != CHIP_NO_ERROR)
                return ESP_FAIL; // Never silently replace corrupted identity.
        } else if (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
            if (key.Serialize(bytes) != CHIP_NO_ERROR ||
                storage->SyncSetKeyValue("hub-leaf-key", bytes.Bytes(), bytes.Length()) != CHIP_NO_ERROR)
                return ESP_FAIL;
        } else {
            return ESP_FAIL;
        }
        err = issuer.GenerateNOCChainAfterValidation(node, fabric, chip::kUndefinedCATs,
                                                     key.Pubkey(), root, intermediate, noc);
        return err == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
    }
};
void install_persistent_issuer() {
    static PersistentIssuer issuer;
    esp_matter::controller::set_custom_credentials_issuer(&issuer);
}
