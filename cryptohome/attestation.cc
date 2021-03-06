// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/attestation.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/data_encoding.h>
#include <brillo/http/http_utils.h>
#include <brillo/mime_utils.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/repeated_field.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/keystore.h"
#include "cryptohome/pkcs11_keystore.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

#include "attestation.pb.h"  // NOLINT(build/include)

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::CombineBlobs;
using brillo::SecureBlob;

namespace {

// Resets a boolean when it falls out of scope.
class ScopedBool {
 public:
  ScopedBool(bool* target, bool reset_value) : target_(target),
                                               reset_value_(reset_value) {}
  ~ScopedBool() { *target_ = reset_value_; }
 private:
  bool* target_;
  bool reset_value_;
};

std::string GetPCAName(cryptohome::Attestation::PCAType pca_type) {
  switch (pca_type) {
    case cryptohome::Attestation::kDefaultPCA:
      return "the default PCA";
    case cryptohome::Attestation::kTestPCA:
      return "the test PCA";
    default: {
      std::ostringstream stream;
      stream << "PCA " << pca_type;
      return stream.str();
    }
  }
}

std::string GetIdentityFeaturesString(int identity_features) {
  unsigned features_count = 0;
  std::ostringstream stream;
  if (identity_features == cryptohome::NO_IDENTITY_FEATURES) {
    stream << cryptohome::IdentityFeatures_Name(
        cryptohome::NO_IDENTITY_FEATURES);
  } else {
    const google::protobuf::EnumDescriptor* desc =
        cryptohome::IdentityFeatures_descriptor();
    for (int i = 0, count = desc->value_count(); i < count; ++i) {
      const google::protobuf::EnumValueDescriptor* value_desc = desc->value(i);
      if (identity_features & value_desc->number()) {
        ++features_count;
        if (stream.tellp() > 0) {
          stream << ", ";
        }
        stream << value_desc->name();
      }
    }
  }
  return std::string("identity feature") + (features_count != 1 ? "s " : " ")
      + stream.str();
}

}  // namespace

namespace cryptohome {

using QuoteMap = google::protobuf::Map<int, Quote>;

// Indexes of PCRs which the created TPM delegate should be bound to.
const uint32_t kDelegateBoundPcrs[] = {0};

const size_t Attestation::kQuoteExternalDataSize = 20;
const size_t Attestation::kCipherKeySize = 32;
#if USE_TPM2
const size_t Attestation::kNonceSize = 32;
const size_t Attestation::kDigestSize = 32;
#else
const size_t Attestation::kNonceSize = 20;  // As per TPM_NONCE definition.
const size_t Attestation::kDigestSize = 20;  // As per TPM_DIGEST definition.
#endif
const size_t Attestation::kChallengeSignatureNonceSize = 20;  // For all TPMs.
const mode_t Attestation::kDatabasePermissions = 0600;
const char Attestation::kDatabaseOwner[] = "attestation";
const char Attestation::kDefaultDatabasePath[] =
    "/mnt/stateful_partition/unencrypted/preserve/attestation.epb";

// This has been extracted from the Chrome OS PCA's encryption certificate
// for the production PCA server.
const char Attestation::kDefaultPCAPublicKey[] =
    "A2976637E113CC457013F4334312A416395B08D4B2A9724FC9BAD65D0290F39C"
    "866D1163C2CD6474A24A55403C968CF78FA153C338179407FE568C6E550949B1"
    "B3A80731BA9311EC16F8F66060A2C550914D252DB90B44D19BC6C15E923FFCFB"
    "E8A366038772803EE57C7D7E5B3D5E8090BF0960D4F6A6644CB9A456708508F0"
    "6C19245486C3A49F807AB07C65D5E9954F4F8832BC9F882E9EE1AAA2621B1F43"
    "4083FD98758745CBFFD6F55DA699B2EE983307C14C9990DDFB48897F26DF8FB2"
    "CFFF03E631E62FAE59CBF89525EDACD1F7BBE0BA478B5418E756FF3E14AC9970"
    "D334DB04A1DF267D2343C75E5D282A287060D345981ABDA0B2506AD882579FEF";

// This value is opaque; it is proprietary to the system managing the private
// key.  In this case the value has been supplied by the PCA maintainers.
// As of this writing, it is a zero (0x00) byte followed by the base64-decoded
// value of the Keymaster hash (doc. added for later memory jogging).
const char Attestation::kDefaultPCAPublicKeyID[] = "\x00\xc7\x0e\x50\xb1";
const char Attestation::kDefaultPCAWebOrigin[] =
    "https://chromeos-ca.gstatic.com";

// This has been extracted from the Chrome OS PCA's encryption certificate
// for the test PCA server.
const char Attestation::kTestPCAPublicKey[] =
    "A1D50D088994000492B5F3ED8A9C5FC8772706219F4C063B2F6A8C6B74D3AD6B"
    "212A53D01DABB34A6261288540D420D3BA59ED279D859DE6227A7AB6BD88FADD"
    "FC3078D465F4DF97E03A52A587BD0165AE3B180FE7B255B7BEDC1BE81CB1383F"
    "E9E46F9312B1EF28F4025E7D332E33F4416525FEB8F0FC7B815E8FBB79CDABE6"
    "327B5A155FEF13F559A7086CB8A543D72AD6ECAEE2E704FF28824149D7F4E393"
    "D3C74E721ACA97F7ADBE2CCF7B4BCC165F7380F48065F2C8370F25F066091259"
    "D14EA362BAF236E3CD8771A94BDEDA3900577143A238AB92B6C55F11DEFAFB31"
    "7D1DC5B6AE210C52B008D87F2A7BFF6EB5C4FB32D6ECEC6505796173951A3167";

// This value is opaque; it is proprietary to the system managing the private
// key.  In this case the value has been supplied by the PCA maintainers.
// As of this writing, it is a zero (0x00) byte followed by the base64-decoded
// value of the Keymaster hash (doc. added for later memory jogging).
const char Attestation::kTestPCAPublicKeyID[] = "\x00\xc2\xb0\x56\x2d";
const char Attestation::kTestPCAWebOrigin[] =
    "https://asbestos-qa.corp.google.com";

#ifdef USE_TEST_PCA
#error "Do not compile with USE_TEST_PCA, pass the right PCA type in calls."
#endif

const char Attestation::kDefaultEnterpriseSigningPublicKey[] =
    "bf7fefa3a661437b26aed0801db64d7ba8b58875c351d3bdc9f653847d4a67b3"
    "b67479327724d56aa0f71a3f57c2290fdc1ff05df80589715e381dfbbda2c4ac"
    "114c30d0a73c5b7b2e22178d26d8b65860aa8dd65e1b3d61a07c81de87c1e7e4"
    "590145624936a011ece10434c1d5d41f917c3dc4b41dd8392479130c4fd6eafc"
    "3bb4e0dedcc8f6a9c28428bf8fbba8bd6438a325a9d3eabee1e89e838138ad99"
    "69c292c6d9f6f52522333b84ddf9471ffe00f01bf2de5faa1621f967f49e158b"
    "f2b305360f886826cc6fdbef11a12b2d6002d70d8d1e8f40e0901ff94c203cb2"
    "01a36a0bd6e83955f14b494f4f2f17c0c826657b85c25ffb8a73599721fa17ab";

const char Attestation::kDefaultEnterpriseEncryptionPublicKey[] =
    "edba5e723da811e41636f792c7a77aef633fbf39b542aa537c93c93eaba7a3b1"
    "0bc3e484388c13d625ef5573358ec9e7fbeb6baaaa87ca87d93fb61bf5760e29"
    "6813c435763ed2c81f631e26e3ff1a670261cdc3c39a4640b6bbf4ead3d6587b"
    "e43ef7f1f08e7596b628ec0b44c9b7ad71c9ee3a1258852c7a986c7614f0c4ec"
    "f0ce147650a53b6aa9ae107374a2d6d4e7922065f2f6eb537a994372e1936c87"
    "eb08318611d44daf6044f8527687dc7ce5319b51eae6ab12bee6bd16e59c499e"
    "fa53d80232ae886c7ee9ad8bc1cbd6e4ac55cb8fa515671f7e7ad66e98769f52"
    "c3c309f98bf08a3b8fbb0166e97906151b46402217e65c5d01ddac8514340e8b";

// This value is opaque; it is proprietary to the system managing the private
// key.  In this case the value has been supplied by the PCA maintainers.
// As of this writing, it is a zero (0x00) byte followed by the base64-decoded
// value of the Keymaster hash (doc. added for later memory jogging).
const char Attestation::kDefaultEnterpriseEncryptionPublicKeyID[] =
    "\x00\x4a\xe2\xdc\xae";

const char Attestation::kTestEnterpriseSigningPublicKey[] =
    "baab3e277518c65b1b98290bb55061df9a50b9f32a4b0ff61c7c61c51e966fcd"
    "c891799a39ee0b7278f204a2b45a7e615080ff8f69f668e05adcf3486b319f80"
    "f9da814d9b86b16a3e68b4ce514ab5591112838a68dc3bfdcc4043a5aa8de52c"
    "ae936847a271971ecaa188172692c13f3b0321239c90559f3b7ba91e66d38ef4"
    "db4c75104ac5f2f15e55a463c49753a88e56906b1725fd3f0c1372beb16d4904"
    "752c74452b0c9f757ee12877a859dd0666cafaccbfc33fe67d98a89a2c12ef52"
    "5e4b16ea8972577dbfc567c2625a3eee6bcaa6cb4939b941f57236d1d57243f8"
    "c9766938269a8034d82fbd44044d2ee6a5c7275589afc3790b60280c0689900f";

const char Attestation::kTestEnterpriseEncryptionPublicKey[] =
    "c0c116e7ded8d7c1e577f9c8fb0d267c3c5c3e3b6800abb0309c248eaa5cd9bf"
    "91945132e4bb0111711356a388b756788e20bc1ecc9261ea9bcae8369cfd050e"
    "d8dc00b50fbe36d2c1c8a9b335f2e11096be76bebce8b5dcb0dc39ac0fd963b0"
    "51474f794d4289cc0c52d0bab451b9e69a43ecd3a84330b0b2de4365c038ffce"
    "ec0f1999d789615849c2f3c29d1d9ed42ccb7f330d5b56f40fb7cc6556190c3b"
    "698c20d83fb341a442fd69701fe0bdc41bdcf8056ccbc8d9b4275e8e43ec6b63"
    "c1ae70d52838dfa90a9cd9e7b6bd88ed3abf4fab444347104e30e635f4f296ac"
    "4c91939103e317d0eca5f36c48102e967f176a19a42220f3cf14634b6773be07";

// This value is opaque; it is proprietary to the system managing the private
// key.  In this case the value has been supplied by the PCA maintainers.
// As of this writing, it is a zero (0x00) byte followed by the base64-decoded
// value of the Keymaster hash (doc. added for later memory jogging).
const char Attestation::kTestEnterpriseEncryptionPublicKeyID[] =
    "\x00\xef\x22\x0f\xb0";

const Attestation::CertificateAuthority Attestation::kKnownEndorsementCA[] = {
  { "IFX TPM EK Intermediate CA 06",
    "de9e58a353313d21d683c687d6aaaab240248717557c077161c5e515f41d8efa"
    "48329f45658fb550f43f91d1ba0c2519429fb6ef964f89657098c90a9783ad6d"
    "3baea625db044734c478768db53b6022c556d8174ed744bd6e4455665715cd5c"
    "beb7c3fcb822ab3dfab1ecee1a628c3d53f6085983431598fb646f04347d5ae0"
    "021d5757cc6e3027c1e13f10633ae48bbf98732c079c17684b0db58bd0291add"
    "e277b037dd13fa3db910e81a4969622a79c85ac768d870f079b54c2b98c856e7"
    "15ef0ba9c01ee1da1241838a1307fe94b1ddfa65cdf7eeaa7e5b4b8a94c3dcd0"
    "29bb5ebcfc935e56641f4c8cb5e726c68f9dd6b41f8602ef6dc78d870a773571" },
  { "IFX TPM EK Intermediate CA 07",
    "f04c9b5b9f3cbc2509179f5e0f31dceb302900f528458e002c3e914d6b29e5e0"
    "924b0bcab2dd053f65d9d4a8eea8269c85c419dba640a88e14dc5f8c8c1a4269"
    "7a5ac4594b36f923110f91d1803d385540c01a433140b06054c77a144ee3a6a6"
    "5950c20f9215be3473b1002eb6b1756a22fbc18d21efacbbc8c270c66cf74982"
    "e24f057825cab51c0dd840a4f2d059032239c33e3f52c6ca06fe49bf4f60cc28"
    "a0fb1173d2ee05a141d30e8ffa32dbb86c1aeb5b309f76c2e462965612ec929a"
    "0d3b04acfa4525912c76f765e948be71f505d619cc673a889f0ed9e1d75f237b"
    "7af6a68550253cb4c3a8ff16c8091dbcbdea0ff8eee3d5bd92f49c53c5a15c93" },
  { "IFX TPM EK Intermediate CA 14",
    "D5B2EB8F8F23DD0B5CA0C15D4376E27A0380FD8EB1E52C2C270D961E8C0F66FD"
    "62E6ED6B3660FFBD8B0735179476F5E9C2EA4C762F5FEEDD3B5EB91785A724BC"
    "4C0617B83966336DD9DC407640871BF99DF4E1701EB5A1F5647FC57879CBB973"
    "B2A72BABA8536B2646A37AA5B73E32A4C8F03E35C8834B391AD363F1F7D1DF2B"
    "EE39233F47384F3E2D2E8EF83C9539B4DFC360C8AEB88B6111E757AF646DC01A"
    "68DAA908C7F8068894E9E991C59005068DD9B0F87113E6A80AB045DB4C1B23FF"
    "38A106098C2E184E1CF42A43EA68753F2649999048E8A3C3406032BEB1457070"
    "BCBE3A93E122638F6F18FF505C35FB827CE5D0C12F27F45C0F59C8A4A8697849" },
  { "IFX TPM EK Intermediate CA 16",
    "B98D42D5284620036A6613ED05A1BE11431AE7DE435EC55F72814652B9265EC2"
    "9035D401B538A9C84BB5B875450FAE8FBEDEF3430C4108D8516404F3DE4D4615"
    "2F471013673A7C7F236304C7363B91C0E0FD9FC7A9EC751521A60A6042839CF7"
    "7AEDE3243D0F51F47ACC39676D236BD5298E18B9A4783C60B2A1CD1B32124909"
    "D5844649EE4539D6AA05A5902C147B4F062D5145708EAE224EC65A8B51D7A418"
    "6327DA8F3B9E7C796F8B2DB3D2BDB39B829BDEBA8D2BF882CBADDB75D76FA8FA"
    "313682688BCD2835533A3A68A4AFDF7E597D8B965402FF22A5A4A418FDB4B549"
    "F218C3908E66BDCEAB3E2FE5EE0A4A1D9EB41A286ED07B6C112581FDAEA088D9" },
  { "IFX TPM EK Intermediate CA 17",
    "B0F3CC6F02E8C0486501102731069644A815F631ED41676C05CE3F7E5E5E40DF"
    "B3BF6D99787F2A9BE8F8B8035C03D5C2226072985230D4CE8407ACD6403F72E1"
    "A4DBF069504E56FA8C0807A704526EAC1E379AE559EB4BBAD9DB4E652B3B14E5"
    "38497A5E7768BCE0BFFAF800C61F1F2262775C526E1790A2BECF9A072A58F6A0"
    "F3042B5279FE9957BCADC3C9725428B66B15D5263F00C528AC47716DE6938199"
    "0FF23BC28F2C33B72D89B5F8EEEF9053B60D230431081D656EA8EC16C7CEFD9E"
    "F5A9061A3C921394D453D9AC77397D59B4C3BAF258266F65559469C3007987D5"
    "A8338E10FC54CD930303C37007D6E1E6C63F36BCFBA1E494AFB3ECD9A2407FF9" },
  { "IFX TPM EK Intermediate CA 21",
    "8149397109974D6C0850C8A60304ED7D209B1B88F435B695394DAD9FB4E64180"
    "02A3940966D2F04103C88659600EEA8E2A5C697C5F989F62D33A06DA10B50075"
    "F37F3CE6AD070413A0E109E16FE652B393C4DAFC5579CCB9915E9A70F5C05BCE"
    "0D341D6B887F43C4334BD8EC6A293FFAB737F77A45069CD0345D3D534E84D029"
    "029C37A267C0CC2D8DCE3E2C76F21A40F5D8D463882A8CBB92D8235685266753"
    "E8F051E78B681E87810A5B21EF719662A8208DFD94C55A126A112E39E0D732D7"
    "3C599095FAFF52BBC0E8C5B3DCD904D05DE00D5C5112F3DF7B76602ABE5DC0F8"
    "F89B55889A24C54EDBA1234AE498BE9B02CB5C8048D1DC90210705BAFC0E2837" },
  { "IFX TPM EK Intermediate CA 29",
    "cd424370776890ace339c62d7faae843bb2c765d27685c0441d278361a929062"
    "b4c95cc57213c864e91cbb92b1151f17a346a4e754c666f2a3e07ea9ffb9c80f"
    "e54d9479f73458c64bf7b0ca4e38821dd318e82d6fe387903ca73ca3e59db48e"
    "fe3b3c7c89599be87bb5e439a6f5843a412d4a321f154955448b71ca0b5fda47"
    "5c86a1c999dde7a01aa16436e65f0b04874c0db3970546bd806157058c5576a5"
    "c00b2bce7173c887f388dc4d5267c68fa5c47fcee3d8491071cd7742d43162cb"
    "285f5ba5e0daa0e910fdce566c5bbf7b3701d51660090344195fd7278456bd98"
    "48382fc5fceaebf93a2ec88c5722723519692e90d23f869c34d8b1af499d4127" },
  { "IFX TPM EK Intermediate CA 30",
    "a01cc43c4b66076d483086d0713a336f435e33ed23d3cda05f3c60a6f707416a"
    "9e53f0ef0de62c82a720e9ad94df29805b56b44279fd7389de4c60d498c81e3b"
    "a27692a045d993e9aaae152768588e5c62213721154529c95b09b201bcb3e573"
    "3d98e398d6e05215867d94e3d222e5b7df9f948c14533285821658b282be4bd7"
    "fe7197baa642f556d4f18738adef26b2eebfc64045cf4c5dcbff661aa95429f4"
    "e2c4921a8723bd8116f0efc038cd4530bb6e9299b7d70327e3fe8790d3d6db3a"
    "ebd3ccd12aef3d43cf89463a28ad1306a9d430b08c3411bfeeda63b9fdcc9a23"
    "1ff5cc203a7f5ee713d50e1930add1cd32ff64637fc740edb63380a5e6725381" },
  { "NTC TPM EK Root CA 01",
    "e836ac61b43e3252d5e1a8a4061997a6a0a272ba3d519d6be6360cc8b4b79e8c"
    "d53c07a7ce9e9310ca84b82bbdad32184544ada357d458cf224c4a3130c97d00"
    "4933b5db232d8b6509412eb4777e9e1b093c58b82b1679c84e57a6b218b4d61f"
    "6dd4c3a66b2dd33b52cb1ffdff543289fa36dd71b7c83b66c1aae37caf7fe88d"
    "851a3523e3ea92b59a6b0ca095c5e1d191484c1bff8a33048c3976e826d4c12a"
    "e198f7199d183e0e70c8b46e8106edec3914397e051ae2b9a7f0b4bb9cd7f2ed"
    "f71064eb0eb473df27b7ccef9a018d715c5fe6ab012a8315f933c7f4fc35d34c"
    "efc27de224b2e3de3b3ba316d5df8b90b2eb879e219d270141b78dbb671a3a05" },
  { "STM TPM EK Intermediate CA 03",
    "a5152b4fbd2c70c0c9a0dd919f48ddcde2b5c0c9988cff3b04ecd844f6cc0035"
    "6c4e01b52463deb5179f36acf0c06d4574327c37572292fcd0f272c2d45ea7f2"
    "2e8d8d18aa62354c279e03be9220f0c3822d16de1ea1c130b59afc56e08f22f1"
    "902a07f881ebea3703badaa594ecbdf8fd1709211ba16769f73e76f348e2755d"
    "bba2f94c1869ef71e726f56f8ece987f345c622e8b5c2a5466d41093c0dc2982"
    "e6203d96f539b542347a08e87fc6e248a346d61a505f52add7f768a5203d70b8"
    "68b6ec92ef7a83a4e6d1e1d259018705755d812175489fae83c4ab2957f69a99"
    "9394ac7a243a5c1cd85f92b8648a8e0d23165fdd86fad06990bfd16fb3293379" },
  /* TODO(ngm): remove by: Aug 30 08:44:33 2020 GMT */
  { "CROS TPM DEV EK ROOT CA",
    "cdc108745dc50dd6a1098c31486fb31578607fd64f64b0d91b994244ca1a9a69"
    "a74c6bccc7f24923e1513e132dc0d9dbcb1b22089299bb6cb669cbf4b704c992"
    "27bb769fa1f91ab11f67fb464a065b34b1a0e824136af5e59d1ac04bda22c199"
    "9f7a5b34bd6b50c81b4a88cc097d4dfeb4dc695096463d9529d69f116e2a26de"
    "070ef3118287072bdbe94466b8737049809bb8e1276b245930051b2bbbad71dd"
    "20d26349d1d83cdb2ff9c65251a17dae4f400ecc3e77f89e27a75fe0709dc81f"
    "e172008a3e65de685d9df43e036c557e88f1a9aedf7a91644391523d9728f946"
    "45c0e8adaf37e9a15777021ad43b675583302402912d66233c59ad05fa3b34ed"
  },
  { "CROS TPM PRD EK ROOT CA",
    "bd6f0198ffa7f7d20c15f81642096e335e2cd74734f73008265fc9957bbe018d"
    "fbac0d2a0ea99f5fb7bbff6f0d367b81199e837c390527972aa5392c2ca0f2a3"
    "506ee7d4a938f47158a7c56a390df2b781344a82b885a62f1de78f37ec105749"
    "69d8abf3163f0cf5c67fa05dd4fb3eb07a7571888b7a87ed57735ce476156bf7"
    "d6eff6cb8c8b303c21ebfe0e11b660edbdf903c70ac16927345d0b38c72f1e60"
    "1460743584f5a3eaef303dbc5cfda48e4c7a1f338108c7f0c70a694f814b6691"
    "ba9d058ab988152bb7097a010e400462187811c3e062001bce8aa808db485bd8"
    "2f7f0e1e2a2ddb95c364dffea4c23e872fc3874c4756e85e6cf8eca6eb6a07bf"
  },
};

const Attestation::CertificateAuthority
    Attestation::kKnownCrosCoreEndorsementCA[] = {
  { "IFX TPM EK Intermediate CA 24",
    "9D3F39677EBDB7B95F383021EA6EF90AD2BEA4E38B10CA65DCD84D0B33D400FA"
    "E7E56FC553975FDADD425227F055C029B6544331E3BA50ED33F6CC02D833EA4E"
    "0EECFE9AD1ADD7095F3A804C560F031E8705A3AD5189CBD62678B5B8205C37ED"
    "780A3EDE8DE64A08980C048872E789937A49FC4048EADCAC9B3FD0F0DD085E76"
    "30DDF9C0C31EFF3B77C6C3601AA7C3DCD10F08616C01435697746A61F920335C"
    "0C45A41149F5D22FCD23DBE35003A9AF7FD91C18715E3709F86A38AB149113C4"
    "D5273C3C90599734FF627ACBF408B082C76E486091F27446E175C50D340DA0FE"
    "5C3FE3D590B8729F4E364E5BF7D854D9AE28EFBCD0CE8F19E6462B3A593983DF" },
  { "IFX TPM EK Intermediate CA 50",
    "ACB01856664D0C81B545DB926D25019FC2D06B4A97DFB91FD7A5AB1A803AA6F4"
    "12FEEE5E3DEF3634172F1271E893C6848B4D156485917DF6F0504947B39F0A5A"
    "E14FFBAB9FF00E70448E51F11DEEA1EA16287ABAAE05D3D00FEB1AA064F1CBD9"
    "E1E67C057087110F9D3023BFA0545C97BD51E473C5B183E50C2984BD9A2DA39B"
    "7D028B895BD939FF0822595DDC948640D06E57ED72EF43B8D8071D2C3C0497A0"
    "EC52F682D1637F06979733BAF56DD809D24C20354D73D3849A1C0DAD23AD5CCB"
    "F8C679242D13FFFE055CC2AB2692897F0329EEA55AF3BB10A4EB4E2937601196"
    "90D64FB352E3D34E05AB53BD4E01EFE3EF56F6DBE315B76A31B0100BF7096093" },
};

const Attestation::PCRValue Attestation::kKnownPCRValues[] = {
  { false, false, kVerified  },
  { false, false, kDeveloper },
  { false, true,  kVerified  },
  { false, true,  kDeveloper },
  { true,  false, kVerified  },
  { true,  false, kDeveloper },
  { true,  true,  kVerified  },
  { true,  true,  kDeveloper }
};

const int Attestation::kNumTemporalValues = 5;

const char Attestation::kAttestationBasedEnterpriseEnrollmentContextName[] =
    "attestation_based_enrollment";

Attestation::Attestation()
    : database_path_(kDefaultDatabasePath),
      pkcs11_key_store_(new Pkcs11KeyStore()),
      key_store_(pkcs11_key_store_.get()),
      install_attributes_observer_(this),
      is_tpm_ready_(false),
      is_prepare_in_progress_(false),
      retain_endorsement_data_(false),
      attestation_user_(0),
      attestation_group_(0) {
  DCHECK(sizeof(kDefaultPCAPublicKey) == 512 + 1);
  DCHECK(sizeof(kTestPCAPublicKey) == 512 + 1);
  DCHECK(sizeof(kDefaultPCAPublicKeyID) == 5 + 1);
  DCHECK(sizeof(kTestPCAPublicKeyID) == 5 + 1);
  DCHECK(sizeof(kDefaultEnterpriseSigningPublicKey) == 512 + 1);
  DCHECK(sizeof(kTestEnterpriseSigningPublicKey) == 512 + 1);
  DCHECK(sizeof(kDefaultEnterpriseEncryptionPublicKey) == 512 + 1);
  DCHECK(sizeof(kTestEnterpriseEncryptionPublicKey) == 512 + 1);
  DCHECK(sizeof(kDefaultEnterpriseEncryptionPublicKeyID) == 5 + 1);
  DCHECK(sizeof(kTestEnterpriseEncryptionPublicKeyID) == 5 + 1);
}

Attestation::~Attestation() {
  if (!thread_.is_null())
    base::PlatformThread::Join(thread_);
  ClearDatabase();
}

void Attestation::set_default_identity_features_for_test(
    int default_identity_features) {
  default_identity_features_ = default_identity_features;
}

void Attestation::Initialize(Tpm* tpm,
                             TpmInit* tpm_init,
                             Platform* platform,
                             Crypto* crypto,
                             InstallAttributes* install_attributes,
                             const SecureBlob& abe_data,
                             bool retain_endorsement_data) {
  base::AutoLock lock(lock_);
  // Inject dependencies.
  tpm_ = tpm;
  tpm_init_ = tpm_init;
  platform_ = platform;
  crypto_ = crypto;
  if (install_attributes_ != install_attributes) {
    install_attributes_ = install_attributes;
    install_attributes_observer_.Add(install_attributes_);
  }
  abe_data_ = abe_data;
  retain_endorsement_data_ = retain_endorsement_data;

  if (tpm_) {
    if (!platform_->GetUserId(kDatabaseOwner, &attestation_user_,
                              &attestation_group_)) {
      LOG(WARNING) << "Attestation: Falling back to root.";
    }
    ExtendPCR1IfClear();
    std::string serial_encrypted_db;
    if (!LoadDatabase(&serial_encrypted_db)) {
      LOG(INFO) << "Attestation: Attestation data not found.";
      return;
    }
    if (!DecryptDatabase(serial_encrypted_db, &database_pb_)) {
      LOG(WARNING) << "Attestation: Attestation data invalid. "
                      "This is normal if the TPM has been cleared.";
      return;
    }
    if (MigrateAttestationDatabase()) {
      if (!PersistDatabaseChanges()) {
        LOG(WARNING) << "Attestation: Failed to persist database changes.";
      }
    }
    FinalizeEndorsementData();
    LOG(INFO) << "Attestation: Valid attestation data exists.";
    // Make sure the owner password is not being held on our account.
    tpm_init_->RemoveTpmOwnerDependency(
        TpmPersistentState::TpmOwnerDependency::kAttestation);
  }
}

bool Attestation::IsPreparedForEnrollment() {
  base::AutoLock lock(lock_);
  if (!database_pb_.has_credentials()) {
    return false;
  }
  return database_pb_.credentials().has_endorsement_credential() ||
    database_pb_.credentials().encrypted_endorsement_credentials().size();
}

bool Attestation::IsPreparedForEnrollmentWith(PCAType pca_type) {
  base::AutoLock lock(lock_);
  return database_pb_.credentials().encrypted_endorsement_credentials().count(
      pca_type);
}

bool Attestation::IsEnrolled() {
  base::AutoLock lock(lock_);
  return HasIdentityCertificate(kFirstIdentity, kDefaultPCA) ||
         HasIdentityCertificate(kFirstIdentity, kTestPCA);
}

Attestation::IdentityCertificateMap::iterator
Attestation::FindIdentityCertificate(int identity, PCAType pca_type) {
  auto end = database_pb_.mutable_identity_certificates()->end();
  for (auto it = database_pb_.mutable_identity_certificates()->begin();
       it != end; ++it) {
    if (it->second.identity() == identity && it->second.aca() == pca_type) {
      return it;
    }
  }
  return end;
}

bool Attestation::HasIdentityCertificate(int identity, PCAType pca_type) {
  return FindIdentityCertificate(identity, pca_type) !=
         database_pb_.mutable_identity_certificates()->end();
}

void Attestation::PrepareForEnrollment() {
  // There are a few ways to trigger PrepareForEnrollment, make sure it only
  // does the work once.
  {
    base::AutoLock lock(lock_);
    if (is_prepare_in_progress_)
      return;
    is_prepare_in_progress_ = true;
  }
  // In the event of an error it is important to reset |is_prepare_in_progress_|
  // so subsequent attempts are possible.
  ScopedBool reset(&is_prepare_in_progress_, false);

  // If there is no TPM, we have no work to do.
  if (!IsTPMReady())
    return;

  // If we are prepared for enrollment, we are done.
  if (IsPreparedForEnrollment())
    return;

  base::TimeTicks start = base::TimeTicks::Now();
  LOG(INFO) << "Attestation: Preparing for enrollment...";
  SecureBlob ek_public_key;
  if (tpm_->GetEndorsementPublicKey(&ek_public_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Attestation: Failed to get EK public key.";
    return;
  }

  base::AutoLock lock(lock_);

  // Compute and store the device EID if needed.
  if (!database_pb_.has_enrollment_id()) {
    ComputeEnterpriseEnrollmentIdInternal(&enterprise_enrollment_id_);
    database_pb_.set_enrollment_id(enterprise_enrollment_id_.data(),
                                   enterprise_enrollment_id_.size());
  }

  // Create a new AIK and PCR quotes for the first identity with default
  // identity features.
  if (CreateIdentity(default_identity_features_, ek_public_key) < 0) {
    return;
  }

  // Encrypt the endorsement credential for all the PCAs we know of.
  TPMCredentials* credentials_pb = database_pb_.mutable_credentials();
  SecureBlob endorsement_credential(credentials_pb->endorsement_credential());
  for (int pca = kDefaultPCA; pca < kMaxPCAType; ++pca) {
    PCAType pca_type = static_cast<PCAType>(pca);
    LOG(INFO) << "Attestation: Encrypting endorsement credential for "
              << GetPCAName(pca_type) << ".";
    if (!EncryptEndorsementCredential(
            pca_type, endorsement_credential,
            &(*credentials_pb
                   ->mutable_encrypted_endorsement_credentials())[pca_type])) {
      LOG(ERROR) << "Attestation: Failed to encrypt EK cert for "
                 << GetPCAName(pca_type) << ".";
      return;
    }
  }

  // Create a delegate so we can activate the AIKs later.
  const std::set<uint32_t> bound_pcrs(std::begin(kDelegateBoundPcrs),
                                      std::end(kDelegateBoundPcrs));
  SecureBlob delegate_blob;
  SecureBlob delegate_secret;
  if (!tpm_->CreateDelegate(bound_pcrs, Tpm::kDefaultDelegateFamilyLabel,
                            Tpm::kDefaultDelegateLabel, &delegate_blob,
                            &delegate_secret)) {
    LOG(ERROR) << "Attestation: Failed to create delegate.";
    return;
  }

  Delegation* delegate_pb = database_pb_.mutable_delegate();
  delegate_pb->set_blob(delegate_blob.data(), delegate_blob.size());
  delegate_pb->set_secret(delegate_secret.data(), delegate_secret.size());
  delegate_pb->set_has_reset_lock_permissions(true);
  delegate_pb->set_can_read_internal_pub(true);

  if (!PersistDatabaseChanges())
    return;

  tpm_init_->RemoveTpmOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kAttestation);
  base::TimeDelta delta = (base::TimeTicks::Now() - start);
  LOG(INFO) << "Attestation: Prepared successfully (" << delta.InMilliseconds()
            << " ms).";
}

int Attestation::CreateIdentity(int identity_features) {
  if (!IsPreparedForEnrollment()) {
    return -1;
  }
  SecureBlob ek_public_key;
  if (GetTpmEndorsementPublicKey(&ek_public_key) != Tpm::kTpmRetryNone) {
    return -1;
  }
  return CreateIdentity(identity_features, ek_public_key);
}

int Attestation::CreateIdentity(int identity_features,
                                const SecureBlob& ek_public_key) {
  // The identity we're creating will have the next index in identities.
  const int identity = database_pb_.identities().size();
  LOG(INFO) << "Attestation: Creating identity " << identity << " with "
            << GetIdentityFeaturesString(identity_features) << ".";
  // Create the AIK.
  SecureBlob identity_public_key_der;
  SecureBlob identity_public_key;
  SecureBlob identity_key_blob;
  SecureBlob identity_binding;
  SecureBlob identity_label;
  SecureBlob pca_public_key;
  SecureBlob endorsement_credential;
  SecureBlob platform_credential;
  SecureBlob conformance_credential;
  if (!tpm_->MakeIdentity(&identity_public_key_der,
                          &identity_public_key,
                          &identity_key_blob,
                          &identity_binding,
                          &identity_label,
                          &pca_public_key,
                          &endorsement_credential,
                          &platform_credential,
                          &conformance_credential)) {
    LOG(ERROR) << "Attestation: Failed to make AIK for identity " << identity
               << ".";
    return -1;
  }

  // This only needs to be done once when we haven't stored credentials yet.
  TPMCredentials* credentials_pb = database_pb_.mutable_credentials();
  if (!credentials_pb->has_endorsement_credential()) {
    credentials_pb->set_endorsement_public_key(ek_public_key.data(),
                                               ek_public_key.size());
    credentials_pb->set_endorsement_credential(endorsement_credential.data(),
                                               endorsement_credential.size());
    credentials_pb->set_platform_credential(platform_credential.data(),
                                            platform_credential.size());
    credentials_pb->set_conformance_credential(conformance_credential.data(),
                                               conformance_credential.size());
  }

  AttestationDatabase::Identity* identity_data =
      database_pb_.mutable_identities()->Add();

  identity_data->set_features(identity_features);

  IdentityKey* key_pb = identity_data->mutable_identity_key();
  key_pb->set_identity_public_key(identity_public_key_der.data(),
                                  identity_public_key_der.size());
  key_pb->set_identity_key_blob(identity_key_blob.data(),
                                identity_key_blob.size());

  if (identity_features & IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) {
    key_pb->set_enrollment_id(database_pb_.enrollment_id());
  }

  IdentityBinding* binding_pb = identity_data->mutable_identity_binding();
  binding_pb->set_identity_binding(identity_binding.data(),
                                   identity_binding.size());
  binding_pb->set_identity_public_key_der(identity_public_key_der.data(),
                                          identity_public_key_der.size());
  binding_pb->set_identity_public_key(identity_public_key.data(),
                                      identity_public_key.size());
  binding_pb->set_identity_label(identity_label.data(), identity_label.size());
  binding_pb->set_pca_public_key(pca_public_key.data(), pca_public_key.size());

  // Store PCR quotes in the identity.
  auto* map = identity_data->mutable_pcr_quotes();

  // Quote PCR0 and PCR1
  for (int i = 0; i <= 1; i++) {
    Quote quote_pb;
    if (!CreatePCRQuote(i, identity_key_blob, &quote_pb)) {
      // Note that if in the future we regularly uses multiple AIK
      // (i.e. identity key), then we'll need to print error messages here to
      // indicate which one of the identity key failed the PCR quote process.
      return -1;
    }

    // PCR1 quote needs to have the source hint
    if (i == 1) {
      quote_pb.set_pcr_source_hint(platform_->GetHardwareID());
    }

    auto in = map->insert(QuoteMap::value_type(i, quote_pb));
    if (!in.second) {
      LOG(ERROR) << "Attestation: Failed to store PCR" << i << " quote for "
                 << "identity " << identity << ".";
      return -1;
    }
  }

  // Return the index of the newly created identity.
  return database_pb_.identities().size() - 1;
}

int Attestation::GetIdentitiesCount() const {
  return database_pb_.identities().size();
}

int Attestation::GetIdentityFeatures(int identity) const {
  return database_pb_.identities().Get(identity).features();
}

Attestation::IdentityCertificateMap Attestation::GetIdentityCertificateMap()
    const {
  base::AutoLock lock(lock_);
  return database_pb_.identity_certificates();
}

void Attestation::PrepareForEnrollmentAsync() {
  base::AutoLock lock(lock_);
  if (!thread_.is_null()) {
    if (!is_prepare_in_progress_) {
      // Join the old thread and reuse the handle.
      base::PlatformThread::Join(thread_);
    } else {
      LOG(WARNING) << "PrepareForEnrollmentAsync called multiple times.";
      return;
    }
  }
  base::PlatformThread::Create(0, this, &thread_);
}

bool Attestation::Verify(bool is_cros_core) {
  if (!IsTPMReady())
    return false;
  LOG(INFO) << "Attestation: Verifying data.";
  base::AutoLock lock(lock_);
  const TPMCredentials& credentials = database_pb_.credentials();
  SecureBlob ek_public_key;
  SecureBlob ek_cert;
  if (credentials.has_endorsement_credential()) {
    ek_cert = SecureBlob(credentials.endorsement_credential());
  } else {
    if (!tpm_->GetEndorsementCredential(&ek_cert)) {
      LOG(ERROR) << "Attestation: Endorsement cert not available.";
      return false;
    }
  }
  if (credentials.has_endorsement_public_key()) {
    ek_public_key = SecureBlob(credentials.endorsement_public_key());
  } else {
    if (tpm_->GetEndorsementPublicKey(&ek_public_key) != Tpm::kTpmRetryNone) {
      LOG(ERROR) << "Attestation: Endorsement key not available.";
      return false;
    }
  }
  if (!VerifyEndorsementCredential(ek_cert, ek_public_key, is_cros_core)) {
    LOG(ERROR) << "Attestation: Bad endorsement credential.";
    return false;
  }
  // Verify() is only used with the first identity.
  const AttestationDatabase::Identity& identity_data =
      database_pb_.identities().Get(kFirstIdentity);
  if (!VerifyIdentityBinding(identity_data.identity_binding())) {
    LOG(ERROR) << "Attestation: Bad identity binding.";
    return false;
  }
  SecureBlob aik_public_key =
      SecureBlob(identity_data.identity_binding().identity_public_key_der());
  if (!VerifyPCR0Quote(aik_public_key, identity_data.pcr_quotes().at(0))) {
    LOG(ERROR) << "Attestation: Bad PCR0 quote.";
    return false;
  }
  if (!VerifyPCR1Quote(aik_public_key, identity_data.pcr_quotes().at(1))) {
    // Don't fail because many devices don't use PCR1.
    LOG(WARNING) << "Attestation: Bad PCR1 quote.";
  }
  SecureBlob nonce;
  if (!tpm_->GetRandomDataSecureBlob(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob identity_key_blob(
      identity_data.identity_key().identity_key_blob());
  SecureBlob public_key;
  SecureBlob public_key_der;
  SecureBlob key_blob;
  SecureBlob key_info;
  SecureBlob proof;
  if (!tpm_->CreateCertifiedKey(identity_key_blob, nonce,
                                &public_key, &public_key_der,
                                &key_blob, &key_info, &proof)) {
    LOG(ERROR) << "Attestation: Failed to create certified key.";
    return false;
  }
  if (!VerifyCertifiedKey(aik_public_key, public_key_der, key_info, proof)) {
    LOG(ERROR) << "Attestation: Bad certified key.";
    return false;
  }
  SecureBlob delegate_blob(database_pb_.delegate().blob());
  SecureBlob delegate_secret(database_pb_.delegate().secret());
  SecureBlob aik_public_key_tpm(
      identity_data.identity_binding().identity_public_key());
  if (!VerifyActivateIdentity(delegate_blob, delegate_secret, identity_key_blob,
                              aik_public_key_tpm, ek_public_key)) {
    LOG(ERROR) << "Attestation: Failed to verify owner delegation.";
    return false;
  }
  LOG(INFO) << "Attestation: Verified OK.";
  return true;
}

bool Attestation::VerifyEK(bool is_cros_core) {
  if (!IsTPMReady())
    return false;
  base::AutoLock lock(lock_);
  SecureBlob ek_cert;
  SecureBlob ek_public_key;
  if (database_pb_.has_credentials()) {
    const TPMCredentials& credentials = database_pb_.credentials();
    if (credentials.has_endorsement_credential()) {
      ek_public_key = SecureBlob(credentials.endorsement_public_key());
      ek_cert = SecureBlob(credentials.endorsement_credential());
    }
  }
  if (ek_cert.size() == 0 && !tpm_->GetEndorsementCredential(&ek_cert)) {
    LOG(ERROR) << __func__ << ": Failed to get EK certificate.";
    return false;
  }
  if (ek_public_key.size() == 0 &&
      tpm_->GetEndorsementPublicKey(&ek_public_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << __func__ << ": Failed to get EK public key.";
    return false;
  }
  return VerifyEndorsementCredential(ek_cert, ek_public_key, is_cros_core);
}

bool Attestation::GetEnterpriseEnrollmentId(
    SecureBlob* enterprise_enrollment_id) {
  return !tpm_->IsTransient(GetEnterpriseEnrollmentIdInternal(
      enterprise_enrollment_id));
}

Tpm::TpmRetryAction Attestation::GetEnterpriseEnrollmentIdInternal(
    SecureBlob* enterprise_enrollment_id) {
  if (!enterprise_enrollment_id_.empty()) {
    *enterprise_enrollment_id = enterprise_enrollment_id_;
    return Tpm::kTpmRetryNone;
  }
  if (database_pb_.has_enrollment_id()) {
    enterprise_enrollment_id_ = SecureBlob(database_pb_.enrollment_id());
    *enterprise_enrollment_id = enterprise_enrollment_id_;
    return Tpm::kTpmRetryNone;
  }
  Tpm::TpmRetryAction action = ComputeEnterpriseEnrollmentIdInternal(
      enterprise_enrollment_id);
  if (action == Tpm::kTpmRetryNone) {
    // Cache the computed value.
    enterprise_enrollment_id_ = *enterprise_enrollment_id;
  }
  return action;
}

bool Attestation::CreateEnrollRequest(PCAType pca_type,
                                      SecureBlob* pca_request) {
  const int identity = kFirstIdentity;
  if (!IsTPMReady())
    return false;
  if (!IsPreparedForEnrollmentWith(pca_type)) {
    LOG(ERROR) << __func__ << ": Enrollment is not possible, attestation data"
               " for " << GetPCAName(pca_type) << " does not exist.";
    return false;
  }
  if (database_pb_.identities().size() <= identity) {
    LOG(ERROR) << __func__ << ": Enrollment is not possible, identity "
               << identity << " does not exist.";
    return false;
  }
  base::AutoLock lock(lock_);
  AttestationEnrollmentRequest request_pb;
  *request_pb.mutable_encrypted_endorsement_credential() =
      database_pb_.credentials().encrypted_endorsement_credentials().at(
          pca_type);
  const AttestationDatabase::Identity& identity_data =
      database_pb_.identities().Get(identity);
  request_pb.set_identity_public_key(
      identity_data.identity_binding().identity_public_key());
  *request_pb.mutable_pcr0_quote() = identity_data.pcr_quotes().at(0);
  *request_pb.mutable_pcr1_quote() = identity_data.pcr_quotes().at(1);

  if (identity_data.features() & IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) {
    SecureBlob enterprise_enrollment_nonce;
    ComputeEnterpriseEnrollmentNonce(&enterprise_enrollment_nonce);
    if (enterprise_enrollment_nonce.empty()) {
      LOG(WARNING)
          << "Attestation: Failed to compute enterprise enrollment nonce.";
    } else {
      request_pb.set_enterprise_enrollment_nonce(
          enterprise_enrollment_nonce.data(),
          enterprise_enrollment_nonce.size());
    }
  }

  std::string tmp;
  if (!request_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  *pca_request = SecureBlob(tmp);
  return true;
}

bool Attestation::Enroll(PCAType pca_type,
                         const SecureBlob& pca_response) {
  const int identity = kFirstIdentity;
  if (!IsTPMReady())
    return false;
  if (database_pb_.identities().size() <= identity) {
    LOG(ERROR) << __func__ << ": Enrollment is not possible, identity "
               << identity << " does not exist.";
    return false;
  }
  AttestationEnrollmentResponse response_pb;
  if (!response_pb.ParseFromArray(pca_response.data(), pca_response.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Privacy CA.";
    return false;
  }
  if (response_pb.status() != OK) {
    LOG(ERROR) << __func__ << ": Error received from Privacy CA: "
               << response_pb.detail();
    return false;
  }
  base::AutoLock lock(lock_);
  SecureBlob delegate_blob(database_pb_.delegate().blob());
  SecureBlob delegate_secret(database_pb_.delegate().secret());
  SecureBlob aik_blob(database_pb_.identities()
                          .Get(identity)
                          .identity_key()
                          .identity_key_blob());
  SecureBlob encrypted_asym(
      response_pb.encrypted_identity_credential().asym_ca_contents());
  SecureBlob encrypted_sym(
      response_pb.encrypted_identity_credential().sym_ca_attestation());
  SecureBlob aik_credential;
  if (!tpm_->ActivateIdentity(delegate_blob, delegate_secret,
                              aik_blob, encrypted_asym, encrypted_sym,
                              &aik_credential)) {
    LOG(ERROR) << __func__ << ": Failed to activate identity " << identity
               << ".";
    return false;
  }

  // Find an identity certificate to reuse or create a new one.
  int index;
  AttestationDatabase_IdentityCertificate* identity_certificate;
  auto found = FindIdentityCertificate(identity, pca_type);
  if (found == database_pb_.mutable_identity_certificates()->end()) {
    index = identity == kFirstIdentity
                ? pca_type
                : std::max(static_cast<size_t>(kMaxPCAType),
                           database_pb_.identity_certificates().size());
    AttestationDatabase::IdentityCertificate new_identity_certificate;
    new_identity_certificate.set_identity(identity);
    new_identity_certificate.set_aca(pca_type);
    auto* map = database_pb_.mutable_identity_certificates();
    auto in = map->insert(
        IdentityCertificateMap::value_type(index, new_identity_certificate));
    if (!in.second) {
      return false;
    }
    found = in.first;
  } else {
    index = found->first;
  }
  identity_certificate = &found->second;

  // Set the credential obtained when activating the identity with the response.
  identity_certificate->set_identity_credential(aik_credential.to_string());

  if (!PersistDatabaseChanges()) {
    LOG(ERROR) << __func__ << ": Failed to persist database changes.";
    return false;
  }
  LOG(INFO) << "Attestation: Enrollment of identity " << identity << " with "
            << GetPCAName(pca_type) << " complete. Certificate #" << index <<
            ".";
  return true;
}

bool Attestation::CreateCertRequest(PCAType pca_type,
                                    CertificateProfile profile,
                                    const std::string& username,
                                    const std::string& origin,
                                    SecureBlob* pca_request) {
  if (!IsTPMReady())
    return false;
  base::AutoLock lock(lock_);
  auto found = FindIdentityCertificate(kFirstIdentity, pca_type);
  if (found == database_pb_.mutable_identity_certificates()->end()) {
    LOG(ERROR) << __func__ << ": Identity " << kFirstIdentity
               << " is not enrolled for attestation with "
               << GetPCAName(pca_type) << ".";
    return false;
  }
  const auto& identity_certificate = found->second;
  AttestationCertificateRequest request_pb;
  request_pb.set_identity_credential(
      identity_certificate.identity_credential());
  SecureBlob message_id(kNonceSize);
  CryptoLib::GetSecureRandom(message_id.data(), message_id.size());
  request_pb.set_message_id(message_id.to_string());
  request_pb.set_profile(profile);
  if (!origin.empty() &&
      (profile == CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID)) {
    request_pb.set_origin(origin);
    request_pb.set_temporal_index(ChooseTemporalIndex(username, origin));
  }
  SecureBlob nonce;
  if (!tpm_->GetRandomDataSecureBlob(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob identity_key_blob(database_pb_.identities()
                                   .Get(identity_certificate.identity())
                                   .identity_key()
                                   .identity_key_blob());
  SecureBlob public_key;
  SecureBlob public_key_der;
  SecureBlob key_blob;
  SecureBlob key_info;
  SecureBlob proof;
  if (!tpm_->CreateCertifiedKey(identity_key_blob, nonce, &public_key,
                                &public_key_der, &key_blob, &key_info,
                                &proof)) {
    LOG(ERROR) << __func__ << ": Failed to create certified key.";
    return false;
  }
  request_pb.set_certified_public_key(public_key.to_string());
  request_pb.set_certified_key_info(key_info.to_string());
  request_pb.set_certified_key_proof(proof.to_string());
  std::string tmp;
  if (!request_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  *pca_request = SecureBlob(tmp);
  ClearString(&tmp);
  // Save certified key blob so we can finish the operation later.
  CertifiedKey certified_key_pb;
  certified_key_pb.set_key_blob(key_blob.to_string());
  certified_key_pb.set_public_key(public_key_der.to_string());
  if (!certified_key_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  pending_cert_requests_[message_id.to_string()] = SecureBlob(tmp);
  ClearString(&tmp);
  return true;
}

bool Attestation::FinishCertRequest(const SecureBlob& pca_response,
                                    bool is_user_specific,
                                    const std::string& username,
                                    const std::string& key_name,
                                    SecureBlob* certificate_chain) {
  if (!IsTPMReady())
    return false;
  AttestationCertificateResponse response_pb;
  if (!response_pb.ParseFromArray(pca_response.data(), pca_response.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Privacy CA.";
    return false;
  }
  base::AutoLock lock(lock_);
  CertRequestMap::iterator iter = pending_cert_requests_.find(
      response_pb.message_id());
  if (iter == pending_cert_requests_.end()) {
    LOG(ERROR) << __func__ << ": Pending request not found.";
    return false;
  }
  if (response_pb.status() != OK) {
    LOG(ERROR) << __func__ << ": Error received from Privacy CA: "
               << response_pb.detail();
    pending_cert_requests_.erase(iter);
    return false;
  }
  CertifiedKey certified_key_pb;
  if (!certified_key_pb.ParseFromArray(iter->second.data(),
                                       iter->second.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse pending request.";
    pending_cert_requests_.erase(iter);
    return false;
  }
  pending_cert_requests_.erase(iter);

  // The PCA issued a certificate and the response matched a pending request.
  // Now we want to finish populating the CertifiedKey and store it for later.
  certified_key_pb.set_certified_key_credential(
      response_pb.certified_key_credential());
  certified_key_pb.set_intermediate_ca_cert(response_pb.intermediate_ca_cert());
  certified_key_pb.mutable_additional_intermediate_ca_cert()->MergeFrom(
      response_pb.additional_intermediate_ca_cert());
  certified_key_pb.set_key_name(key_name);
  if (!SaveKey(is_user_specific, username, key_name, certified_key_pb))
    return false;
  LOG(INFO) << "Attestation: Certified key credential received and stored.";
  return CreatePEMCertificateChain(certified_key_pb, certificate_chain);
}

bool Attestation::GetCertificateChain(bool is_user_specific,
                                      const std::string& username,
                                      const std::string& key_name,
                                      SecureBlob* certificate_chain) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Could not find certified key: " << key_name;
    return false;
  }
  return CreatePEMCertificateChain(key, certificate_chain);
}

bool Attestation::GetPublicKey(bool is_user_specific,
                               const std::string& username,
                               const std::string& key_name,
                               SecureBlob* public_key) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Could not find certified key: " << key_name;
    return false;
  }
  SecureBlob public_key_der(key.public_key());

  // Convert from PKCS #1 RSAPublicKey to X.509 SubjectPublicKeyInfo.
  const unsigned char* asn1_ptr = public_key_der.data();
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, public_key_der.size()));
  if (!rsa.get()) {
    LOG(ERROR) << __func__ << ": Failed to decode public key.";
    return false;
  }
  unsigned char* buffer = NULL;
  int length = i2d_RSA_PUBKEY(rsa.get(), &buffer);
  if (length <= 0) {
    LOG(ERROR) << __func__ << ": Failed to encode public key.";
    return false;
  }
  SecureBlob tmp(buffer, buffer + length);
  brillo::SecureMemset(buffer, 0, length);
  OPENSSL_free(buffer);
  public_key->swap(tmp);
  return true;
}

bool Attestation::DoesKeyExist(bool is_user_specific,
                               const std::string& username,
                               const std::string& key_name) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  return FindKeyByName(is_user_specific, username, key_name, &key);
}

bool Attestation::SignEnterpriseChallenge(
      bool is_user_specific,
      const std::string& username,
      const std::string& key_name,
      const std::string& domain,
      const SecureBlob& device_id,
      bool include_signed_public_key,
      const SecureBlob& challenge,
      SecureBlob* response) {
  return SignEnterpriseVaChallenge(kDefaultVA, is_user_specific, username,
                                 key_name, domain, device_id,
                                 include_signed_public_key, challenge,
                                 response);
}

bool Attestation::SignEnterpriseVaChallenge(
      VAType va_type,
      bool is_user_specific,
      const std::string& username,
      const std::string& key_name,
      const std::string& domain,
      const SecureBlob& device_id,
      bool include_signed_public_key,
      const SecureBlob& challenge,
      SecureBlob* response) {
  if (!IsTPMReady())
    return false;
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Key not found.";
    return false;
  }

  // Validate that the challenge is coming from the expected source.
  SignedData signed_challenge;
  if (!signed_challenge.ParseFromArray(challenge.data(),
                                       challenge.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse signed challenge.";
    return false;
  }
  if (!ValidateEnterpriseChallenge(va_type, signed_challenge)) {
    LOG(ERROR) << __func__ << ": Invalid challenge.";
    return false;
  }

  // Assemble a response protobuf.
  ChallengeResponse response_pb;
  *response_pb.mutable_challenge() = signed_challenge;
  SecureBlob nonce;
  if (!tpm_->GetRandomDataSecureBlob(kChallengeSignatureNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": Failed to generate nonce.";
    return false;
  }
  response_pb.set_nonce(nonce.to_string());
  KeyInfo key_info;
  // EUK -> Enterprise User Key
  // EMK -> Enterprise Machine Key
  key_info.set_key_type(is_user_specific ? EUK : EMK);
  key_info.set_domain(domain);
  key_info.set_device_id(device_id.to_string());
  // Only include the certificate if this is a user key.
  if (is_user_specific) {
    SecureBlob certificate_chain;
    if (!CreatePEMCertificateChain(key, &certificate_chain)) {
      LOG(ERROR) << __func__ << ": Failed to construct certificate chain.";
      return false;
    }
    key_info.set_certificate(certificate_chain.to_string());
  }
  if (is_user_specific && include_signed_public_key) {
    SecureBlob spkac;
    if (!CreateSignedPublicKey(key, &spkac)) {
      LOG(ERROR) << __func__ << ": Failed to create signed public key.";
      return false;
    }
    key_info.set_signed_public_key_and_challenge(spkac.to_string());
  }
  if (!EncryptEnterpriseKeyInfo(va_type, key_info,
                                response_pb.mutable_encrypted_key_info())) {
    LOG(ERROR) << __func__ << ": Failed to encrypt KeyInfo.";
    return false;
  }

  // Serialize and sign the response protobuf.
  std::string serialized;
  if (!response_pb.SerializeToString(&serialized)) {
    LOG(ERROR) << __func__ << ": Failed to serialize response protobuf.";
    return false;
  }
  SecureBlob input_data(serialized);
  ClearString(&serialized);
  if (!SignChallengeData(key, input_data, response)) {
    LOG(ERROR) << __func__ << ": Failed to sign data.";
    return false;
  }
  return true;
}

bool Attestation::SignSimpleChallenge(bool is_user_specific,
                                      const std::string& username,
                                      const std::string& key_name,
                                      const SecureBlob& challenge,
                                      SecureBlob* response) {
  if (!IsTPMReady())
    return false;
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Key not found.";
    return false;
  }
  // Add a nonce to ensure this service cannot be used to sign arbitrary data.
  SecureBlob nonce;
  if (!tpm_->GetRandomDataSecureBlob(kChallengeSignatureNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": Failed to generate nonce.";
    return false;
  }
  SecureBlob input_data = SecureBlob::Combine(challenge, nonce);
  if (!SignChallengeData(key, input_data, response)) {
    LOG(ERROR) << __func__ << ": Failed to sign data.";
    return false;
  }
  return true;
}

bool Attestation::RegisterKey(bool is_user_specific,
                              const std::string& username,
                              const std::string& key_name,
                              bool include_certificates) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Key not found.";
    return false;
  }
  SecureBlob certificate;
  if (include_certificates) {
    certificate = SecureBlob(key.certified_key_credential());
  }
  if (!key_store_->Register(is_user_specific,
                            username,
                            key_name,
                            SecureBlob(key.key_blob()),
                            SecureBlob(key.public_key()),
                            certificate)) {
    LOG(ERROR) << __func__ << ": Failed to register key.";
    return false;
  }
  if (include_certificates) {
    if (key.has_intermediate_ca_cert()) {
      if (!key_store_->RegisterCertificate(
          is_user_specific,
          username,
          SecureBlob(key.intermediate_ca_cert()))) {
        LOG(WARNING) << __func__ << ": Failed to register certificate.";
      }
    }
    for (int i = 0; i < key.additional_intermediate_ca_cert_size(); ++i) {
      if (!key_store_->RegisterCertificate(
          is_user_specific,
          username,
          SecureBlob(key.additional_intermediate_ca_cert(i)))) {
        LOG(WARNING) << __func__ << ": Failed to register certificate.";
      }
    }
  }
  // Once registered with key store, we don't want to keep our copy.
  DeleteKey(is_user_specific, username, key_name);
  return true;
}

bool Attestation::GetKeyPayload(bool is_user_specific,
                                const std::string& username,
                                const std::string& key_name,
                                SecureBlob* payload) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Key not found.";
    return false;
  }
  SecureBlob tmp(key.payload());
  payload->swap(tmp);
  return true;
}

bool Attestation::SetKeyPayload(bool is_user_specific,
                                const std::string& username,
                                const std::string& key_name,
                                const SecureBlob& payload) {
  base::AutoLock lock(lock_);
  CertifiedKey key;
  if (!FindKeyByName(is_user_specific, username, key_name, &key)) {
    LOG(ERROR) << __func__ << ": Key not found.";
    return false;
  }
  key.set_payload(payload.to_string());
  return SaveKey(is_user_specific, username, key_name, key);
}

bool Attestation::DeleteKeysByPrefix(bool is_user_specific,
                                     const std::string& username,
                                     const std::string& key_prefix) {
  base::AutoLock lock(lock_);
  if (is_user_specific) {
    return key_store_->DeleteByPrefix(is_user_specific, username, key_prefix);
  }
  // Manipulate the device keys protobuf field.  Linear time strategy is to swap
  // all elements we want to keep to the front and then truncate.
  google::protobuf::RepeatedPtrField<CertifiedKey>* device_keys =
      database_pb_.mutable_device_keys();
  int next_keep_index = 0;
  for (int i = 0; i < device_keys->size(); ++i) {
    if (device_keys->Get(i).key_name().find(key_prefix) != 0) {
      // Prefix doesn't match -> keep.
      if (i != next_keep_index)
        device_keys->SwapElements(next_keep_index, i);
      ++next_keep_index;
    }
  }
  while (next_keep_index < device_keys->size()) {
    device_keys->RemoveLast();
  }
  return PersistDatabaseChanges();
}

bool Attestation::GetEKInfo(std::string* ek_info) {
  if (!IsTPMReady())
    return false;
  SecureBlob ek_cert;
  if (!tpm_->GetEndorsementCredential(&ek_cert)) {
    LOG(ERROR) << "Cannot get EK certificate from TPM.  EK info not available.";
    return false;
  }
  SecureBlob hash = CryptoLib::Sha256(ek_cert);
  *ek_info = base::StringPrintf(
      "EK Certificate:\n%s\nHash:\n%s\n",
      CreatePEMCertificate(ek_cert.to_string()).c_str(),
      base::HexEncode(hash.data(), hash.size()).c_str());
  return true;
}

bool Attestation::GetIdentityResetRequest(const std::string& reset_token,
                                          SecureBlob* reset_request) {
  base::AutoLock lock(lock_);
  AttestationResetRequest proto;
  proto.set_token(reset_token);
  // This only works with the default PCA right now because the method does
  // note take a PCA type. As far as we know, this call isn't supported either.
  *proto.mutable_encrypted_endorsement_credential() =
      (*database_pb_.mutable_credentials()
            ->mutable_encrypted_endorsement_credentials())[kDefaultPCA];
  std::string serial;
  if (!proto.SerializeToString(&serial)) {
    LOG(ERROR) << __func__ << "Failed to serialize protobuf.";
    return false;
  }
  SecureBlob tmp(serial);
  ClearString(&serial);
  reset_request->swap(tmp);
  return true;
}

bool Attestation::IsPCR0VerifiedMode() {
  if (!IsTPMReady())
    return false;
  Blob current_pcr_value;
  if (!tpm_->ReadPCR(0, &current_pcr_value) ||
      current_pcr_value.size() != kDigestSize) {
    LOG(WARNING) << "Failed to read PCR0.";
    return false;
  }
  Blob settings_blob(3);
  settings_blob[0] = false;  // Developer mode enabled.
  settings_blob[1] = false;  // Recovery mode enabled.
  settings_blob[2] = kVerified;  // Firmware type.
  const Blob settings_digest = CryptoLib::Sha1(settings_blob);
  const Blob extend_pcr_value =
      CombineBlobs({Blob(kDigestSize), settings_digest});
  const Blob expected_pcr_value = CryptoLib::Sha1(extend_pcr_value);
  return current_pcr_value == expected_pcr_value;
}

bool Attestation::EncryptDatabase(const AttestationDatabase& db,
                                  std::string* serial_encrypted_db) {
  CHECK(crypto_);
  std::string serial_string;
  if (!db.SerializeToString(&serial_string)) {
    LOG(ERROR) << "Failed to serialize db.";
    return false;
  }
  SecureBlob serial_data(serial_string.begin(), serial_string.end());
  if (database_key_.empty() || sealed_database_key_.empty()) {
    if (!crypto_->CreateSealedKey(&database_key_, &sealed_database_key_)) {
      LOG(ERROR) << "Failed to generate database key.";
      return false;
    }
  }
  if (!crypto_->EncryptData(serial_data, database_key_, sealed_database_key_,
                            serial_encrypted_db)) {
    LOG(ERROR) << "Attestation: Failed to encrypt database.";
    return false;
  }
  return true;
}

bool Attestation::DecryptDatabase(const std::string& serial_encrypted_db,
                                  AttestationDatabase* db) {
  CHECK(crypto_);
  if (!crypto_->UnsealKey(serial_encrypted_db, &database_key_,
                          &sealed_database_key_)) {
    LOG(ERROR) << "Attestation: Could not unseal decryption key.";
    return false;
  }
  SecureBlob serial_blob;
  if (!crypto_->DecryptData(serial_encrypted_db, database_key_, &serial_blob)) {
    LOG(ERROR) << "Attestation: Failed to decrypt database with Tpm.";
    return false;
  }
  std::string serial_string = serial_blob.to_string();
  if (!db->ParseFromString(serial_string)) {
    // Previously the DB was encrypted with CryptoLib::AesEncrypt which appends
    // a SHA-1.  This can be safely ignored.
    const size_t kLegacyJunkSize = 20;
    if (serial_string.size() < kLegacyJunkSize ||
        !db->ParseFromArray(serial_string.data(),
                            serial_string.length() - kLegacyJunkSize)) {
      LOG(ERROR) << "Failed to parse database.";
      return false;
    }
  }
  return true;
}

bool Attestation::StoreDatabase(const std::string& serial_encrypted_db) {
  if (!platform_->WriteStringToFileAtomicDurable(database_path_,
                                                 serial_encrypted_db,
                                                 kDatabasePermissions)) {
    LOG(ERROR) << "Failed to write db.";
    return false;
  }
  if (!platform_->SetOwnership(
          database_path_, attestation_user_, attestation_group_, true)) {
    PLOG(ERROR) << "Failed to set db ownership";
    return false;
  }
  return true;
}

bool Attestation::LoadDatabase(std::string* serial_encrypted_db) {
  CheckDatabasePermissions();
  if (!platform_->ReadFileToString(database_path_, serial_encrypted_db)) {
    PLOG(ERROR) << "Failed to read db.";
    return false;
  }
  return true;
}

bool Attestation::PersistDatabaseChanges() {
  return PersistDatabase(database_pb_);
}

bool Attestation::PersistDatabase(const AttestationDatabase& db) {
  std::string serial_encrypted_db;
  if (!EncryptDatabase(db, &serial_encrypted_db)) {
    LOG(ERROR) << "Attestation: Failed to encrypt db.";
    return false;
  }
  if (!StoreDatabase(serial_encrypted_db)) {
    LOG(ERROR) << "Attestation: Failed to store db.";
    return false;
  }
  return true;
}

void Attestation::CheckDatabasePermissions() {
  const mode_t kMask = 0007;  // No permissions for 'others'.
  CHECK(platform_);
  mode_t permissions = 0;
  uid_t user = 0;
  gid_t group = 0;
  if (!platform_->GetPermissions(database_path_, &permissions) ||
      !platform_->GetOwnership(database_path_, &user, &group, true)) {
    if (errno != ENOENT) {
      PLOG(WARNING) << "Failed to read database permissions";
    }
    return;
  }
  if ((permissions & kMask) != 0) {
    LOG(WARNING) << "Fixing database permissions.";
    if (!platform_->SetPermissions(database_path_, permissions & ~kMask)) {
      PLOG(WARNING) << "Failed to fix database permissions";
    }
  }
  if (user != attestation_user_ || group != attestation_group_) {
    LOG(WARNING) << "Fixing database ownership.";
    if (!platform_->SetOwnership(database_path_,
                                 attestation_user_,
                                 attestation_group_,
                                 true /* follow links */)) {
      PLOG(WARNING) << "Failed to fix database ownership";
    }
  }
}

bool Attestation::VerifyEndorsementCredential(const SecureBlob& credential,
                                              const SecureBlob& public_key,
                                              bool is_cros_core) {
  const unsigned char* asn1_ptr = credential.data();
  std::unique_ptr<X509, X509Deleter> x509(
      d2i_X509(NULL, &asn1_ptr, credential.size()));
  if (!x509.get()) {
    LOG(ERROR) << "Failed to parse endorsement credential.";
    return false;
  }
  // Manually verify the certificate signature.
  char issuer[100];  // A longer CN will truncate.
  X509_NAME_get_text_by_NID(x509.get()->cert_info->issuer,
                            NID_commonName,
                            issuer,
                            arraysize(issuer));
  std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter> issuer_key =
      GetAuthorityPublicKey(issuer, is_cros_core);
  if (!issuer_key.get()) {
    LOG(ERROR) << "Unknown endorsement credential issuer.";
    return false;
  }
  if (X509_verify(x509.get(), issuer_key.get()) != 1) {
    LOG(ERROR) << "Bad endorsement credential signature.";
    return false;
  }
  // Verify that the given public key matches the public key in the credential.
  // Note: Do not use any openssl functions that attempt to decode the public
  // key. These will fail because openssl does not recognize the OAEP key type.
  auto public_key_data = x509.get()->cert_info->key->public_key->data;
  SecureBlob credential_public_key(
      public_key_data,
      public_key_data + x509.get()->cert_info->key->public_key->length);
  if (credential_public_key.size() != public_key.size() ||
      memcmp(credential_public_key.data(),
             public_key.data(),
             public_key.size()) != 0) {
    LOG(ERROR) << "Bad endorsement credential public key.";
    return false;
  }
  return true;
}

bool Attestation::VerifyIdentityBinding(const IdentityBinding& binding) {
  // Reconstruct and hash a serialized TPM_IDENTITY_CONTENTS structure.
  const unsigned char header[] = {1, 1, 0, 0, 0, 0, 0, 0x79};
  std::string label_ca = binding.identity_label() + binding.pca_public_key();
  SecureBlob label_ca_digest = CryptoLib::Sha1(
      SecureBlob(label_ca));
  ClearString(&label_ca);
  // The signed data is header + digest + pubkey.
  SecureBlob contents = SecureBlob::Combine(SecureBlob::Combine(
      SecureBlob(std::begin(header), std::end(header)),
      label_ca_digest),
      SecureBlob(binding.identity_public_key()));
  // Now verify the signature.
  if (!VerifySignature(SecureBlob(
      binding.identity_public_key_der()),
      contents,
      SecureBlob(binding.identity_binding()))) {
    LOG(ERROR) << "Failed to verify identity binding signature.";
    return false;
  }
  return true;
}

bool Attestation::VerifyPCR0Quote(const SecureBlob& aik_public_key,
                                  const Quote& quote) {
  if (!VerifyQuoteSignature(aik_public_key, quote, 0)) {
    return false;
  }

  // Check if the PCR0 value represents a known mode.
  for (size_t i = 0; i < arraysize(kKnownPCRValues); ++i) {
    Blob settings_blob(3);
    settings_blob[0] = kKnownPCRValues[i].developer_mode_enabled;
    settings_blob[1] = kKnownPCRValues[i].recovery_mode_enabled;
    settings_blob[2] = kKnownPCRValues[i].firmware_type;
    const Blob settings_digest = CryptoLib::Sha1(settings_blob);
    const Blob extend_pcr_value =
        CombineBlobs({Blob(kDigestSize), settings_digest});
    const Blob final_pcr_value = CryptoLib::Sha1(extend_pcr_value);
    if (BlobFromString(quote.quoted_pcr_value()) == final_pcr_value) {
      std::string description = "Developer Mode: ";
      description += kKnownPCRValues[i].developer_mode_enabled ? "On" : "Off";
      description += ", Recovery Mode: ";
      description += kKnownPCRValues[i].recovery_mode_enabled ? "On" : "Off";
      description += ", Firmware Type: ";
      description += (kKnownPCRValues[i].firmware_type == 1) ? "Verified" :
                     "Developer";
      LOG(INFO) << "PCR0: " << description;
      return true;
    }
  }
  LOG(WARNING) << "PCR0 value not recognized.";
  return true;
}

bool Attestation::VerifyPCR1Quote(const SecureBlob& aik_public_key,
                                  const Quote& quote) {
  if (!VerifyQuoteSignature(aik_public_key, quote, 1)) {
    return false;
  }

  // Check that the source hint is correctly populated.
  std::string hwid = platform_->GetHardwareID();
  if (hwid != quote.pcr_source_hint()) {
    LOG(ERROR) << "PCR1 source hint does not match HWID: " << hwid;
    return false;
  }

  LOG(INFO) << "PCR1 verified as " << hwid;
  return true;
}

bool Attestation::VerifyQuoteSignature(const SecureBlob& aik_public_key,
                                       const Quote& quote,
                                       uint32_t pcr_index) {
  if (!VerifySignature(aik_public_key,
                       SecureBlob(quote.quoted_data()),
                       SecureBlob(quote.quote()))) {
    LOG(ERROR) << "Failed to verify quote signature.";
    return false;
  }

  // Check that the quoted value matches the given PCR value. We can verify this
  // by reconstructing the TPM_PCR_COMPOSITE structure the TPM would create.
  CHECK_LE(pcr_index, 16);
  CHECK_LE(quote.quoted_pcr_value().size(), 256U);
  const uint8_t header[] = {
    // TPM_PCR_SELECTION.sizeOfSelect: 16-bit length of PCR index bitmap.
    uint8_t(0), uint8_t(2),
    // TPM_PCR_SELECTION.pcrSelect: PCR index bitmap.
    uint8_t(pcr_index < 8 ? (1 << pcr_index) : 0),
    uint8_t(pcr_index < 8 ? 0 : (1 << (pcr_index - 8))),
    // TPM_PCR_COMPOSITE.valueSize: 32-bit length of PCR value.
    uint8_t(0), uint8_t(0), uint8_t(0),
    uint8_t(quote.quoted_pcr_value().size())};
  const Blob pcr_composite =
      CombineBlobs({Blob(std::begin(header), std::end(header)),
                    BlobFromString(quote.quoted_pcr_value())});
  const Blob pcr_digest = CryptoLib::Sha1(pcr_composite);
  SecureBlob quoted_data(quote.quoted_data());
  // The PCR digest should appear 8 bytes into the quoted data. See the
  // TPM_QUOTE_INFO structure.
  if (search(quoted_data.begin(), quoted_data.end(),
             pcr_digest.begin(), pcr_digest.end()) != quoted_data.begin() + 8) {
    LOG(ERROR) << "PCR value mismatch.";
    return false;
  }

  if (quote.has_pcr_source_hint()) {
    // Check if the PCR value matches the hint.
    const Blob hint_digest =
        CryptoLib::Sha256(BlobFromString(quote.pcr_source_hint()));
    const Blob hint_digest_prefix(hint_digest.begin(),
                                  hint_digest.begin() + 20);
    const Blob extend_pcr_value =
        CombineBlobs({Blob(kDigestSize), hint_digest_prefix});
    const Blob final_pcr_value = CryptoLib::Sha1(extend_pcr_value);
    if (BlobFromString(quote.quoted_pcr_value()) != final_pcr_value) {
      LOG(ERROR) << "PCR source hint is invalid.";
      return false;
    }
  }
  return true;
}

bool Attestation::VerifyCertifiedKey(
    const SecureBlob& aik_public_key,
    const SecureBlob& certified_public_key,
    const SecureBlob& certified_key_info,
    const SecureBlob& proof) {
  std::string key_info = certified_key_info.to_string();
  if (!VerifySignature(aik_public_key, certified_key_info, proof)) {
    LOG(ERROR) << "Failed to verify certified key proof signature.";
    return false;
  }
  const unsigned char* asn1_ptr = certified_public_key.data();
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, certified_public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode certified public key.";
    return false;
  }
  SecureBlob modulus(BN_num_bytes(rsa.get()->n));
  BN_bn2bin(rsa.get()->n, modulus.data());
  SecureBlob key_digest = CryptoLib::Sha1(modulus);
  if (std::search(certified_key_info.begin(),
                  certified_key_info.end(),
                  key_digest.begin(),
                  key_digest.end()) == certified_key_info.end()) {
    LOG(ERROR) << "Certified public key mismatch.";
    return false;
  }
  return true;
}

std::unique_ptr<EVP_PKEY, Attestation::EVP_PKEYDeleter>
    Attestation::GetAuthorityPublicKey(const char* issuer_name,
                                       bool is_cros_core) {
  const CertificateAuthority* const kKnownCA =
      is_cros_core ? kKnownCrosCoreEndorsementCA : kKnownEndorsementCA;
  const int kNumIssuers =
      is_cros_core ? arraysize(kKnownCrosCoreEndorsementCA) :
                     arraysize(kKnownEndorsementCA);
  for (int i = 0; i < kNumIssuers; ++i) {
    if (0 == strcmp(issuer_name, kKnownCA[i].issuer)) {
      std::unique_ptr<RSA, RSADeleter> rsa = CreateRSAFromHexModulus(
          kKnownCA[i].modulus);
      std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter> pkey(EVP_PKEY_new());
      if (!pkey.get()) {
        return std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter>();
      }
      EVP_PKEY_assign_RSA(pkey.get(), rsa.release());
      return pkey;
    }
  }
  return std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter>();
}

bool Attestation::VerifySignature(const SecureBlob& public_key,
                                  const SecureBlob& signed_data,
                                  const SecureBlob& signature) {
  const unsigned char* asn1_ptr = public_key.data();
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }
  SecureBlob digest = CryptoLib::Sha1(signed_data);
  if (!RSA_verify(NID_sha1, digest.data(), digest.size(),
                  signature.data(), signature.size(), rsa.get())) {
    LOG(ERROR) << "Failed to verify signature.";
    return false;
  }
  return true;
}

bool Attestation::MigrateIdentityData() {
  if (database_pb_.identities().size() > 0) {
    // We already migrated identity data.
    return false;
  }

  // The identity we're creating will have the next index in identities.
  LOG(INFO) << "Attestation: Migrating existing identity into identity "
            << database_pb_.identities().size() << ".";
  AttestationDatabase::Identity identity_data;
  identity_data.set_features(IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID);
  if (database_pb_.has_identity_binding()) {
    identity_data.mutable_identity_binding()->CopyFrom(
        database_pb_.identity_binding());
  } else {
    LOG(ERROR) << "Attestation: Identity Key Binding not found, not migrating.";
    return false;
  }

  if (database_pb_.has_identity_key()) {
    identity_data.mutable_identity_key()->CopyFrom(
        database_pb_.identity_key());
    identity_data.mutable_identity_key()->clear_identity_credential();
    // Migration for the other part of the identity_key is moved to the end of
    // the function, this is so that we don't have to rollback too many things
    // when there's an error.
  } else {
    LOG(ERROR) << "Attestation: Identity Key not found, not migrating.";
    return false;
  }

  for (int i = 0; i <= 1; i++) {
    Quote quote_pb;
    if (i == 0 && database_pb_.has_pcr0_quote()) {
      quote_pb.CopyFrom(database_pb_.pcr0_quote());
    } else if (i == 1 && database_pb_.has_pcr1_quote()) {
      quote_pb.CopyFrom(database_pb_.pcr1_quote());
    } else {
      // Attempt to generate the missing PCR quote.
      SecureBlob identity_key_blob(
        identity_data.identity_key().identity_key_blob());
      if (CreatePCRQuote(i, identity_key_blob, &quote_pb)) {
        if (i == 1) {
          quote_pb.set_pcr_source_hint(platform_->GetHardwareID());
        }
        LOG(WARNING) << "Attestation: Regenerated missing PCR" << i
                     << " quote during migration.";
      } else {
        LOG(ERROR) << "Attestation: Failed to regenerate missing PCR"
                   << i << " quote during migration.";
        return false;
      }
    }

    // If we arrive here, quote_pb is definitely filled.
    auto in = identity_data.mutable_pcr_quotes()->insert(
        QuoteMap::value_type(i, quote_pb));
    if (!in.second) {
      LOG(ERROR) << "Attestation: Could not migrate existing identity.";
      return false;
    }
  }

  // Migrate the other part of identity_key, note that since we are here,
  // identity_key is guaranteed to exist.
  if (database_pb_.identity_key().has_identity_credential()) {
    // Create an identity certificate for this identity and the default PCA.
    AttestationDatabase::IdentityCertificate identity_certificate;
    identity_certificate.set_identity(kFirstIdentity);
    identity_certificate.set_aca(kDefaultPCA);
    identity_certificate.set_identity_credential(
      database_pb_.identity_key().identity_credential());

    auto* map = database_pb_.mutable_identity_certificates();
    auto in = map->insert(IdentityCertificateMap::value_type(
      kDefaultPCA, identity_certificate));
    if (!in.second) {
      LOG(ERROR) << "Attestation: Could not migrate existing identity.";
      return false;
    }
  }
  if (database_pb_.identity_key().has_enrollment_id()) {
    database_pb_.set_enrollment_id(
      database_pb_.identity_key().enrollment_id());
  }

  // If we are here, we can be sure that identity_data actually holds a
  // valid identity object, so we save it back into the DB.
  AttestationDatabase::Identity* new_identity_data =
    database_pb_.mutable_identities()->Add();
  new_identity_data->CopyFrom(identity_data);

  return true;
}

void Attestation::ClearDatabase() {
  TPMCredentials* credentials = database_pb_.mutable_credentials();
  ClearString(credentials->mutable_endorsement_public_key());
  ClearString(credentials->mutable_endorsement_credential());
  ClearString(credentials->mutable_platform_credential());
  ClearString(credentials->mutable_conformance_credential());
  ClearString(database_pb_.mutable_enrollment_id());
  for (auto it = database_pb_.mutable_identities()->begin();
       it != database_pb_.mutable_identities()->end(); ++it) {
    ClearIdentity(&*it);
  }
  for (auto it = database_pb_.mutable_identity_certificates()->begin();
       it != database_pb_.mutable_identity_certificates()->end(); ++it) {
    ClearIdentityCertificate(&it->second);
  }
  Delegation* delegate = database_pb_.mutable_delegate();
  ClearString(delegate->mutable_blob());
  ClearString(delegate->mutable_secret());
  database_pb_.Clear();
}

void Attestation::ClearQuote(Quote* quote) {
  ClearString(quote->mutable_quote());
  ClearString(quote->mutable_quoted_data());
  ClearString(quote->mutable_quoted_pcr_value());
  ClearString(quote->mutable_pcr_source_hint());
}

void Attestation::ClearIdentityCertificate(
    AttestationDatabase::IdentityCertificate* identity_certificate) {
  ClearString(identity_certificate->mutable_identity_credential());
}

void Attestation::ClearIdentity(AttestationDatabase::Identity* identity) {
  auto binding = identity->mutable_identity_binding();
  ClearString(binding->mutable_identity_binding());
  ClearString(binding->mutable_identity_public_key_der());
  ClearString(binding->mutable_identity_public_key());
  ClearString(binding->mutable_identity_label());
  ClearString(binding->mutable_pca_public_key());

  auto key = identity->mutable_identity_key();
  ClearString(key->mutable_identity_public_key());
  ClearString(key->mutable_identity_key_blob());
  ClearString(key->mutable_identity_credential());
  ClearString(key->mutable_enrollment_id());

  auto end = identity->mutable_pcr_quotes()->end();
  for (auto it = identity->mutable_pcr_quotes()->begin(); it != end; ++it) {
    ClearQuote(&it->second);
  }
}

void Attestation::ClearString(std::string* s) {
  brillo::SecureMemset(base::string_as_array(s), 0, s->length());
  s->clear();
}

bool Attestation::VerifyActivateIdentity(const SecureBlob& delegate_blob,
                                         const SecureBlob& delegate_secret,
                                         const SecureBlob& identity_key_blob,
                                         const SecureBlob& identity_public_key,
                                         const SecureBlob& ek_public_key) {
  const char* kTestCredential = "test";
  const uint8_t kAlgAES256 = 9;  // This comes from TPM_ALG_AES256.
  const uint8_t kEncModeCBC = 2;  // This comes from TPM_SYM_MODE_CBC.
  const uint8_t kAsymContentHeader[] =
      {0, 0, 0, kAlgAES256, 0, kEncModeCBC, 0, kCipherKeySize};
  const uint8_t kSymContentHeader[12] = {0};

  // Generate an AES key and encrypt the credential.
  SecureBlob aes_key(kCipherKeySize);
  CryptoLib::GetSecureRandom(aes_key.data(), aes_key.size());
  SecureBlob credential(kTestCredential,
                        kTestCredential + strlen(kTestCredential));
  SecureBlob encrypted_credential;
  if (!TssCompatibleEncrypt(aes_key, credential, &encrypted_credential)) {
    LOG(ERROR) << "Failed to encrypt credential.";
    return false;
  }

  // Construct a TPM_ASYM_CA_CONTENTS structure.
  SecureBlob public_key_digest = CryptoLib::Sha1(identity_public_key);
  SecureBlob asym_content = SecureBlob::Combine(SecureBlob::Combine(
      SecureBlob(std::begin(kAsymContentHeader), std::end(kAsymContentHeader)),
      aes_key),
      public_key_digest);

  // Encrypt the TPM_ASYM_CA_CONTENTS with the EK public key.
  const unsigned char* asn1_ptr = ek_public_key.data();
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, ek_public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode EK public key.";
    return false;
  }
  SecureBlob encrypted_asym_content;
  if (!CryptoLib::TpmCompatibleOAEPEncrypt(rsa.get(), asym_content,
                                           &encrypted_asym_content)) {
    LOG(ERROR) << "Failed to encrypt with EK public key.";
    return false;
  }

  // Construct a TPM_SYM_CA_ATTESTATION structure.
  uint32_t length = htonl(encrypted_credential.size());
  SecureBlob length_blob(sizeof(uint32_t));
  memcpy(length_blob.data(), &length, sizeof(uint32_t));
  SecureBlob sym_content = SecureBlob::Combine(SecureBlob::Combine(
      length_blob,
      SecureBlob(std::begin(kSymContentHeader), std::end(kSymContentHeader))),
      encrypted_credential);

  // Attempt to activate the identity.
  SecureBlob credential_out;
  if (!tpm_->ActivateIdentity(delegate_blob, delegate_secret, identity_key_blob,
                              encrypted_asym_content, sym_content,
                              &credential_out)) {
    LOG(ERROR) << "Failed to activate identity.";
    return false;
  }
  if (credential.size() != credential_out.size() ||
      brillo::SecureMemcmp(credential.data(), credential_out.data(),
                             credential.size()) != 0) {
    LOG(ERROR) << "Invalid identity credential.";
    return false;
  }
  return true;
}

bool Attestation::EncryptEndorsementCredential(
    PCAType pca_type,
    const SecureBlob& credential,
    EncryptedData* encrypted_credential) {
  std::unique_ptr<RSA, RSADeleter> rsa;
  std::string key_id;
  switch (pca_type) {
    case kDefaultPCA:
      rsa = CreateRSAFromHexModulus(kDefaultPCAPublicKey);
      key_id = std::string(kDefaultPCAPublicKeyID,
                           arraysize(kDefaultPCAPublicKeyID) - 1);
      break;
    case kTestPCA:
      rsa = CreateRSAFromHexModulus(kTestPCAPublicKey);
      key_id = std::string(kTestPCAPublicKeyID,
                           arraysize(kTestPCAPublicKeyID) - 1);
      break;
    default:
      NOTREACHED();
  }
  if (!rsa.get()) {
    LOG(ERROR) << __func__ << ": Failed to decode public key.";
    return false;
  }
  return EncryptData(credential, rsa.get(), key_id, encrypted_credential);
}

bool Attestation::AddDeviceKey(const std::string& key_name,
                               const CertifiedKey& key) {
  // If a key by this name already exists, reuse the field.
  bool found = false;
  for (int i = 0; i < database_pb_.device_keys_size(); ++i) {
    if (database_pb_.device_keys(i).key_name() == key_name) {
      found = true;
      *database_pb_.mutable_device_keys(i) = key;
      break;
    }
  }
  if (!found)
    *database_pb_.add_device_keys() = key;
  return PersistDatabaseChanges();
}

void Attestation::RemoveDeviceKey(const std::string& key_name) {
  bool found = false;
  for (int i = 0; i < database_pb_.device_keys_size(); ++i) {
    if (database_pb_.device_keys(i).key_name() == key_name) {
      found = true;
      int last = database_pb_.device_keys_size() - 1;
      if (i < last) {
        database_pb_.mutable_device_keys()->SwapElements(i, last);
      }
      database_pb_.mutable_device_keys()->RemoveLast();
      break;
    }
  }
  if (found) {
    if (!PersistDatabaseChanges()) {
      LOG(WARNING) << __func__ << ": Failed to persist key deletion.";
    }
  }
}

bool Attestation::FindKeyByName(bool is_user_specific,
                                const std::string& username,
                                const std::string& key_name,
                                CertifiedKey* key) {
  if (is_user_specific) {
    SecureBlob key_data;
    if (!key_store_->Read(is_user_specific, username, key_name, &key_data)) {
      LOG(INFO) << "Key not found: " << key_name;
      return false;
    }
    if (!key->ParseFromArray(key_data.data(), key_data.size())) {
      LOG(ERROR) << "Failed to parse key: " << key_name;
      return false;
    }
    return true;
  }
  for (int i = 0; i < database_pb_.device_keys_size(); ++i) {
    if (database_pb_.device_keys(i).key_name() == key_name) {
      *key = database_pb_.device_keys(i);
      return true;
    }
  }
  LOG(INFO) << "Key not found: " << key_name;
  return false;
}

bool Attestation::SaveKey(bool is_user_specific,
                          const std::string& username,
                          const std::string& key_name,
                          const CertifiedKey& key) {
  if (is_user_specific) {
    std::string tmp;
    if (!key.SerializeToString(&tmp)) {
      LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
      return false;
    }
    SecureBlob blob(tmp);
    ClearString(&tmp);
    if (!key_store_->Write(is_user_specific, username, key_name, blob)) {
      LOG(ERROR) << __func__ << ": Failed to store certified key for user.";
      return false;
    }
  } else {
    if (!AddDeviceKey(key_name, key)) {
      LOG(ERROR) << __func__ << ": Failed to store certified key for device.";
      return false;
    }
  }
  return true;
}

void Attestation::DeleteKey(bool is_user_specific,
                            const std::string& username,
                            const std::string& key_name) {
  if (is_user_specific) {
    key_store_->Delete(is_user_specific, username, key_name);
  } else {
    RemoveDeviceKey(key_name);
  }
}

bool Attestation::CreatePEMCertificateChain(const CertifiedKey& key,
                                            SecureBlob* certificate_chain) {
  if (key.certified_key_credential().empty()) {
    LOG(ERROR) << "Certificate is empty.";
    return false;
  }
  std::string pem = CreatePEMCertificate(key.certified_key_credential());
  if (!key.intermediate_ca_cert().empty()) {
    pem += "\n";
    pem += CreatePEMCertificate(key.intermediate_ca_cert());
  }
  for (int i = 0; i < key.additional_intermediate_ca_cert_size(); ++i) {
    pem += "\n";
    pem += CreatePEMCertificate(key.additional_intermediate_ca_cert(i));
  }
  *certificate_chain = SecureBlob(pem);
  ClearString(&pem);
  return true;
}

std::string Attestation::CreatePEMCertificate(const std::string& certificate) {
  const char kBeginCertificate[] = "-----BEGIN CERTIFICATE-----\n";
  const char kEndCertificate[] = "-----END CERTIFICATE-----";

  std::string pem = kBeginCertificate;
  pem += brillo::data_encoding::Base64EncodeWrapLines(certificate);
  pem += kEndCertificate;
  return pem;
}

bool Attestation::SignChallengeData(const CertifiedKey& key,
                                    const SecureBlob& data_to_sign,
                                    SecureBlob* response) {
  SecureBlob signature;
  if (!tpm_->Sign(SecureBlob(key.key_blob()),
                  data_to_sign,
                  kNotBoundToPCR,
                  &signature)) {
    LOG(ERROR) << "Failed to generate signature.";
    return false;
  }
  SignedData signed_data;
  signed_data.set_data(data_to_sign.to_string());
  signed_data.set_signature(signature.to_string());
  std::string serialized;
  if (!signed_data.SerializeToString(&serialized)) {
    LOG(ERROR) << "Failed to serialize signed data.";
    return false;
  }
  SecureBlob tmp(serialized);
  ClearString(&serialized);
  response->swap(tmp);
  return true;
}

bool Attestation::ValidateEnterpriseChallenge(
    VAType va_type,
    const SignedData& signed_challenge) {
  RSA* signing_key = GetEnterpriseSigningKey(va_type);
  if (!signing_key)
    return false;
  const char kExpectedChallengePrefix[] = "EnterpriseKeyChallenge";
  SecureBlob digest = CryptoLib::Sha256(SecureBlob(signed_challenge.data()));
  SecureBlob signature(signed_challenge.signature());
  if (!RSA_verify(NID_sha256, digest.data(), digest.size(),
                  signature.data(), signature.size(), signing_key)) {
    LOG(ERROR) << "Failed to verify challenge signature.";
    return false;
  }
  Challenge challenge;
  if (!challenge.ParseFromString(signed_challenge.data())) {
    LOG(ERROR) << "Failed to parse challenge protobuf.";
    return false;
  }
  if (challenge.prefix() != kExpectedChallengePrefix) {
    LOG(ERROR) << "Unexpected challenge prefix.";
    return false;
  }
  return true;
}

bool Attestation::EncryptEnterpriseKeyInfo(VAType va_type,
                                           const KeyInfo& key_info,
                                           EncryptedData* encrypted_data) {
  std::string serialized;
  if (!key_info.SerializeToString(&serialized)) {
    LOG(ERROR) << "Failed to serialize key info.";
    return false;
  }
  RSA* enterprise_key = GetEnterpriseEncryptionKey(va_type);
  bool result = EncryptData(SecureBlob(serialized),
                            enterprise_key,
                            GetEnterpriseEncryptionPublicKeyID(va_type),
                            encrypted_data);
  ClearString(&serialized);
  return result;
}

RSA* Attestation::GetEnterpriseSigningKey(Attestation::VAType va_type) {
  auto search = enterprise_signing_keys_.find(va_type);
  if (search != enterprise_signing_keys_.end())
    return search->second.get();
  // Create the key and remember it in the keys map.
  std::unique_ptr<RSA, RSADeleter> rsa = CreateRSAFromHexModulus(
      va_type == kDefaultVA ? kDefaultEnterpriseSigningPublicKey
          : kTestEnterpriseSigningPublicKey);
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public signing key.";
    return nullptr;
  }
  auto inserted = enterprise_signing_keys_.insert(
      KeysMap::value_type(va_type, std::move(rsa)));
  if (!inserted.second) {
    LOG(ERROR) << "Failed to insert public signing key in map.";
    return nullptr;
  }
  return inserted.first->second.get();
}

RSA* Attestation::GetEnterpriseEncryptionKey(Attestation::VAType va_type) {
  auto search = enterprise_encryption_keys_.find(va_type);
  if (search != enterprise_encryption_keys_.end())
    return search->second.get();
  // Create the key and remember it in the keys map.
  std::unique_ptr<RSA, RSADeleter> rsa = CreateRSAFromHexModulus(
      va_type == kDefaultVA ? kDefaultEnterpriseEncryptionPublicKey
          : kTestEnterpriseEncryptionPublicKey);
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public encryption key.";
    return nullptr;
  }
  auto inserted = enterprise_encryption_keys_.insert(
      KeysMap::value_type(va_type, std::move(rsa)));
  if (!inserted.second) {
    LOG(ERROR) << "Failed to insert public encryption key in map.";
    return nullptr;
  }
  return inserted.first->second.get();
}

std::string Attestation::GetEnterpriseEncryptionPublicKeyID(
    Attestation::VAType va_type) const {
  return std::string(
      va_type == kDefaultVA ? kDefaultEnterpriseEncryptionPublicKeyID
          : kTestEnterpriseEncryptionPublicKeyID,
      arraysize(va_type == kDefaultVA ? kDefaultEnterpriseEncryptionPublicKeyID
                            : kTestEnterpriseEncryptionPublicKeyID) - 1);
}

void Attestation::set_enterprise_test_keys(VAType va_type,
                                           RSA* signing_key,
                                           RSA* encryption_key) {
  enterprise_signing_keys_[va_type] =
      std::unique_ptr<RSA, RSADeleter>(signing_key);
  enterprise_encryption_keys_[va_type] =
      std::unique_ptr<RSA, RSADeleter>(encryption_key);
}

bool Attestation::EncryptData(const SecureBlob& input,
                              RSA* wrapping_key,
                              const std::string& wrapping_key_id,
                              EncryptedData* output) {
  // Encrypt with a randomly generated AES key.
  SecureBlob aes_key;
  if (!tpm_->GetRandomDataSecureBlob(kCipherKeySize, &aes_key)) {
    LOG(ERROR) << __func__ << ": GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob aes_iv;
  if (!tpm_->GetRandomDataSecureBlob(kAesBlockSize, &aes_iv)) {
    LOG(ERROR) << __func__ << ": GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob encrypted;
  if (!AesEncrypt(input, aes_key, aes_iv, &encrypted)) {
    LOG(ERROR) << "AesEncrypt failed.";
    return false;
  }
  output->set_encrypted_data(encrypted.data(), encrypted.size());
  output->set_iv(aes_iv.data(), aes_iv.size());
  output->set_wrapping_key_id(wrapping_key_id);
  output->set_mac(CryptoLib::ComputeEncryptedDataHMAC(*output, aes_key));

  // Wrap the AES key with the given public key.
  SecureBlob encrypted_key(RSA_size(wrapping_key));
  int length = RSA_public_encrypt(aes_key.size(),
                                  aes_key.data(),
                                  encrypted_key.data(),
                                  wrapping_key, RSA_PKCS1_OAEP_PADDING);
  if (length == -1) {
    LOG(ERROR) << "RSA_public_encrypt failed.";
    return false;
  }
  encrypted_key.resize(length);
  output->set_wrapped_key(encrypted_key.to_string());
  return true;
}

std::unique_ptr<RSA, Attestation::RSADeleter>
Attestation::CreateRSAFromHexModulus(
    const std::string& hex_modulus) {
  std::unique_ptr<RSA, RSADeleter> rsa(RSA_new());
  if (!rsa.get())
    return std::unique_ptr<RSA, RSADeleter>();
  rsa->e = BN_new();
  if (!rsa->e)
    return std::unique_ptr<RSA, RSADeleter>();
  BN_set_word(rsa->e, kWellKnownExponent);
  rsa->n = BN_new();
  if (!rsa->n)
    return std::unique_ptr<RSA, RSADeleter>();
  if (0 == BN_hex2bn(&rsa->n, hex_modulus.c_str()))
    return std::unique_ptr<RSA, RSADeleter>();
  return rsa;
}

bool Attestation::CreateSignedPublicKey(
    const CertifiedKey& key,
    brillo::SecureBlob* signed_public_key) {
  // Get the certified public key as an EVP_PKEY.
  const unsigned char* asn1_ptr =
    reinterpret_cast<const unsigned char*>(key.public_key().data());
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, key.public_key().size()));
  if (!rsa.get())
    return false;
  std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter> public_key(EVP_PKEY_new());
  if (!public_key.get())
    return false;
  EVP_PKEY_assign_RSA(public_key.get(), rsa.release());

  // Fill in the public key.
  std::unique_ptr<NETSCAPE_SPKI, NETSCAPE_SPKIDeleter>
      spki(NETSCAPE_SPKI_new());
  if (!spki.get())
    return false;
  if (!NETSCAPE_SPKI_set_pubkey(spki.get(), public_key.get()))
    return false;

  // Fill in a random challenge.
  SecureBlob challenge;
  if (!tpm_->GetRandomDataSecureBlob(kNonceSize, &challenge))
    return false;
  std::string challenge_hex = base::HexEncode(challenge.data(),
                                              challenge.size());
  if (!ASN1_STRING_set(spki.get()->spkac->challenge,
                       challenge_hex.data(),
                       challenge_hex.size()))
    return false;

  // Generate the signature.
  unsigned char* buffer = NULL;
  int length = i2d_NETSCAPE_SPKAC(spki.get()->spkac, &buffer);
  if (length <= 0)
    return false;
  SecureBlob data_to_sign(buffer, buffer + length);
  OPENSSL_free(buffer);
  SecureBlob signature;
  if (!tpm_->Sign(SecureBlob(key.key_blob()),
                  data_to_sign,
                  kNotBoundToPCR,
                  &signature)) {
    return false;
  }

  // Fill in the signature and algorithm.
  if (!ASN1_BIT_STRING_set(spki.get()->signature,
                           reinterpret_cast<unsigned char*>(signature.data()),
                           signature.size())) {
    return false;
  }
  // Be explicit that there are zero unused bits; otherwise i2d below will
  // automatically detect unused bits but signatures require zero unused bits.
  spki.get()->signature->flags = ASN1_STRING_FLAG_BITS_LEFT;
  X509_ALGOR_set0(spki.get()->sig_algor,
                  OBJ_nid2obj(NID_sha256WithRSAEncryption),
                  V_ASN1_NULL,
                  NULL);

  // DER encode.
  buffer = NULL;
  length = i2d_NETSCAPE_SPKI(spki.get(), &buffer);
  if (length <= 0)
    return false;
  SecureBlob tmp(buffer, buffer + length);
  OPENSSL_free(buffer);
  signed_public_key->swap(tmp);

  return true;
}

bool Attestation::AesEncrypt(const brillo::SecureBlob& plaintext,
                             const brillo::SecureBlob& key,
                             const brillo::SecureBlob& iv,
                             brillo::SecureBlob* ciphertext) {
  return CryptoLib::AesEncryptSpecifyBlockMode(
      plaintext, 0, plaintext.size(), key, iv, CryptoLib::kPaddingStandard,
      CryptoLib::kCbc, ciphertext);
}

bool Attestation::AesDecrypt(const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& key,
                             const brillo::SecureBlob& iv,
                             brillo::SecureBlob* plaintext) {
  return CryptoLib::AesDecryptSpecifyBlockMode(
      ciphertext, 0, ciphertext.size(), key, iv, CryptoLib::kPaddingStandard,
      CryptoLib::kCbc, plaintext);
}

bool Attestation::TssCompatibleEncrypt(const SecureBlob& key,
                                       const SecureBlob& input,
                                       SecureBlob* output) {
  CHECK(output);
  if (key.size() != kCipherKeySize) {
    LOG(ERROR) << "Wrong key size!";
    return false;
  }
  SecureBlob iv(AES_BLOCK_SIZE);
  CryptoLib::GetSecureRandom(reinterpret_cast<unsigned char*>(iv.data()),
                             AES_BLOCK_SIZE);
  SecureBlob encrypted_input;
  if (!AesEncrypt(input, key, iv, &encrypted_input)) {
    LOG(ERROR) << "Error encrypting input.";
    return false;
  }
  *output = SecureBlob::Combine(iv, encrypted_input);
  return true;
}

int Attestation::ChooseTemporalIndex(const std::string& user,
                                     const std::string& origin) {
  std::string user_hash = CryptoLib::Sha256(SecureBlob(user)).to_string();
  std::string origin_hash = CryptoLib::Sha256(SecureBlob(origin)).to_string();
  int histogram[kNumTemporalValues] = {0};
  for (int i = 0; i < database_pb_.temporal_index_record_size(); ++i) {
    const AttestationDatabase::TemporalIndexRecord& record =
        database_pb_.temporal_index_record(i);
    // Ignore out-of-range index values.
    if (record.temporal_index() < 0 ||
        record.temporal_index() >= kNumTemporalValues)
      continue;
    if (record.origin_hash() == origin_hash) {
      if (record.user_hash() == user_hash) {
        // We've previously chosen this index for this user, reuse it.
        return record.temporal_index();
      } else {
        // We've previously chosen this index for another user.
        ++histogram[record.temporal_index()];
      }
    }
  }
  int least_used_index = 0;
  for (int i = 1; i < kNumTemporalValues; ++i) {
    if (histogram[i] < histogram[least_used_index])
      least_used_index = i;
  }
  if (histogram[least_used_index] > 0) {
    LOG(WARNING) << "Unique origin-specific identifiers have been exhausted.";
    ReportCrosEvent(kAttestationOriginSpecificIdentifiersExhausted);
  }
  // Record our choice for later reference.
  AttestationDatabase::TemporalIndexRecord* new_record =
      database_pb_.add_temporal_index_record();
  new_record->set_origin_hash(origin_hash);
  new_record->set_user_hash(user_hash);
  new_record->set_temporal_index(least_used_index);
  PersistDatabaseChanges();
  return least_used_index;
}

bool Attestation::MigrateAttestationDatabase() {
  bool migrated = false;

  if (database_pb_.has_credentials()) {
    if (!database_pb_.credentials().encrypted_endorsement_credentials().count(
            kDefaultPCA) &&
        database_pb_.credentials()
            .has_default_encrypted_endorsement_credential()) {
      LOG(INFO) << "Attestation: Migrating endorsement credential for "
                << GetPCAName(kDefaultPCA) << ".";
      (*database_pb_.mutable_credentials()
            ->mutable_encrypted_endorsement_credentials())[kDefaultPCA] =
          database_pb_.credentials().default_encrypted_endorsement_credential();
      migrated = true;
    }
    if (!database_pb_.credentials().encrypted_endorsement_credentials().count(
            kTestPCA) &&
        database_pb_.credentials()
            .has_test_encrypted_endorsement_credential()) {
      LOG(INFO) << "Attestation: Migrating endorsement credential for "
                << GetPCAName(kTestPCA) << ".";
      (*database_pb_.mutable_credentials()
            ->mutable_encrypted_endorsement_credentials())[kTestPCA] =
          database_pb_.credentials().test_encrypted_endorsement_credential();
      migrated = true;
    }
  }

  // Migrate identity data if needed.
  migrated |= MigrateIdentityData();

  return migrated;
}

void Attestation::FinalizeEndorsementData() {
  if (retain_endorsement_data_) {
    return;
  }
  // Only finalize endorsement data after install attributes are finalized.
  if (install_attributes_->is_first_install()) {
    return;
  }
  if (!database_pb_.has_credentials()) {
    return;
  }
  TPMCredentials* credentials = database_pb_.mutable_credentials();
  if (!credentials->has_endorsement_credential()) {
    return;
  }
  for (int pca = kDefaultPCA; pca < kMaxPCAType; ++pca) {
    if (!credentials->mutable_encrypted_endorsement_credentials()->count(pca)) {
      LOG(INFO) << "Attestation: Migrating endorsement data for "
                << GetPCAName(static_cast<PCAType>(pca)) << ".";
      if (!EncryptEndorsementCredential(
              static_cast<PCAType>(pca),
              SecureBlob(credentials->endorsement_credential()),
              &(*credentials
                     ->mutable_encrypted_endorsement_credentials())[pca])) {
        LOG(ERROR) << "Attestation: Failed to encrypt EK cert for "
                   << GetPCAName(static_cast<PCAType>(pca)) << ".";
        return;
      }
    }
  }
  LOG(INFO) << "Attestation: Clearing endorsement data.";
  ClearString(credentials->mutable_endorsement_public_key());
  credentials->clear_endorsement_public_key();
  ClearString(credentials->mutable_endorsement_credential());
  credentials->clear_endorsement_credential();
  if (!PersistDatabaseChanges()) {
    LOG(ERROR) << "Attestation: Failed to persist database changes.";
  }
}

bool Attestation::GetDelegateCredentials(brillo::SecureBlob* blob,
                                         brillo::SecureBlob* secret,
                                         bool* has_reset_lock_permissions) {
  if (!database_pb_.has_delegate()) {
    return false;
  }
  SecureBlob tmp_blob(database_pb_.delegate().blob());
  blob->swap(tmp_blob);
  SecureBlob tmp_secret(database_pb_.delegate().secret());
  secret->swap(tmp_secret);
  *has_reset_lock_permissions =
      database_pb_.delegate().has_reset_lock_permissions();
  return true;
}

bool Attestation::GetCachedEndorsementData(
    brillo::SecureBlob* ek_public_key,
    brillo::SecureBlob* ek_certificate) {
  if (!database_pb_.has_credentials()) {
    return false;
  }
  const TPMCredentials& credentials = database_pb_.credentials();
  // If the TPM is not owned it's possible we have access to the public key but
  // not to the certificate.
  if (!credentials.has_endorsement_public_key()) {
    return false;
  }
  SecureBlob tmp_public_key(credentials.endorsement_public_key());
  ek_public_key->swap(tmp_public_key);
  SecureBlob tmp_credential(credentials.endorsement_credential());
  ek_certificate->swap(tmp_credential);
  return true;
}

void Attestation::CacheEndorsementData() {
  // Before taking ownership we can only cache the public key. After taking
  // ownership this is taken care of by the prepare-for-enrollment process.
  if (tpm_->IsOwned() ||
      (database_pb_.has_credentials() &&
       database_pb_.credentials().has_endorsement_public_key())) {
    return;
  }
  SecureBlob public_key_blob;
  if (tpm_->GetEndorsementPublicKey(&public_key_blob) != Tpm::kTpmRetryNone) {
    LOG(WARNING) << "TPM is not owned but failed to cache EK public key.";
    return;
  }
  database_pb_.mutable_credentials()->set_endorsement_public_key(
      public_key_blob.data(),
      public_key_blob.size());
}

bool Attestation::IsTPMReady() {
  if (!is_tpm_ready_ && tpm_)
    is_tpm_ready_ = tpm_->IsEnabled() &&
                    tpm_->IsOwned() &&
                    !tpm_->IsBeingOwned();
  return is_tpm_ready_;
}

void Attestation::ExtendPCR1IfClear() {
  Blob current_pcr_value;
  if (!tpm_->ReadPCR(1, &current_pcr_value) ||
      current_pcr_value.size() != kDigestSize) {
    LOG(WARNING) << "Failed to read PCR1.";
    return;
  }
  Blob default_pcr_value(kDigestSize);
  if (current_pcr_value != default_pcr_value) {
    // The PCR has already been extended.
    return;
  }
  std::string hwid = platform_->GetHardwareID();
  LOG(WARNING) << "Extending PCR1.";
  // Take the first n bytes of a SHA-256 hash because this is what firmware
  // would do. (Using SHA-256 allows a single precomputed hash to be stored
  // along with the HWID for both TPM 1.2 and 2.0 platforms).
  const Blob hwid_hash = CryptoLib::Sha256(BlobFromString(hwid));
  const Blob extension(hwid_hash.begin(), hwid_hash.begin() + kDigestSize);
  if (hwid.length() == 0 || !tpm_->ExtendPCR(1, extension)) {
    LOG(WARNING) << "Failed to extend PCR1.";
  }
}

bool Attestation::CreatePCRQuote(
    uint32_t pcr_index,
    const SecureBlob& identity_key_blob,
    Quote* output) {
  SecureBlob external_data(kQuoteExternalDataSize);
  CryptoLib::GetSecureRandom(external_data.data(), kQuoteExternalDataSize);

  Blob quoted_pcr_value;
  SecureBlob quoted_data;
  SecureBlob quote;

  if (!tpm_->QuotePCR(pcr_index,
                      identity_key_blob,
                      external_data,
                      &quoted_pcr_value,
                      &quoted_data,
                      &quote)) {
    LOG(ERROR) << "Attestation: Failed to generate PCR" << pcr_index
               << " quote.";
    return false;
  }

  output->set_quote(quote.data(), quote.size());
  output->set_quoted_data(quoted_data.data(), quoted_data.size());
  output->set_quoted_pcr_value(BlobToString(quoted_pcr_value));

  return true;
}

bool Attestation::SendPCARequestAndBlock(PCAType pca_type,
                                         PCARequestType request_type,
                                         const brillo::SecureBlob& request,
                                         brillo::SecureBlob* reply) {
  std::shared_ptr<brillo::http::Transport> transport = http_transport_;
  if (!transport) {
    transport = brillo::http::Transport::CreateDefault();
  }
  std::unique_ptr<brillo::http::Response> response = PostBinaryAndBlock(
      GetPCAURL(pca_type, request_type),
      request.data(),
      request.size(),
      brillo::mime::application::kOctet_stream,
      {},  // headers
      transport,
      NULL);  // error
  if (!response->IsSuccessful()) {
    LOG(ERROR) << "HTTP request to PCA failed.";
    return false;
  }
  std::vector<uint8_t> response_data = response->ExtractData();
  SecureBlob tmp(response_data.begin(), response_data.end());
  reply->swap(tmp);
  return true;
}

std::string Attestation::GetPCAURL(PCAType pca_type,
                                   PCARequestType request_type) const {
  std::string url;
  switch (pca_type) {
    case kDefaultPCA:
      url = kDefaultPCAWebOrigin;
      break;
    case kTestPCA:
      url = kTestPCAWebOrigin;
      break;
    default:
      NOTREACHED();
  }

  switch (request_type) {
    case kEnroll:
      url += "/enroll";
      break;
    case kGetCertificate:
      url += "/sign";
      break;
    default:
      NOTREACHED();
  }
  return url;
}

void Attestation::ComputeEnterpriseEnrollmentNonce(
    brillo::SecureBlob* enterprise_enrollment_nonce) {
  if (abe_data_.empty()) {
    // If there is no ABE data we cannot compute the DEN. We do not
    // want to fail attestation for those devices.
    enterprise_enrollment_nonce->clear();
    return;
  }
  const uint8_t* context_name = reinterpret_cast<const uint8_t*>(
      kAttestationBasedEnterpriseEnrollmentContextName);
  SecureBlob context_key(context_name, context_name
      + sizeof(kAttestationBasedEnterpriseEnrollmentContextName) - 1);
  SecureBlob nonce = CryptoLib::HmacSha256(context_key, abe_data_);
  enterprise_enrollment_nonce->swap(nonce);
}

Tpm::TpmRetryAction Attestation::GetTpmEndorsementPublicKey(
    SecureBlob* ek_public_key) {
  Tpm::TpmRetryAction action = Tpm::kTpmRetryFailNoRetry;
  if (database_pb_.has_delegate()) {
    // Try to call using the delegate unless we know that it does not have
    // proper permissions.
    if (!database_pb_.delegate().has_can_read_internal_pub()
        || database_pb_.delegate().can_read_internal_pub())  {
      SecureBlob delegate_blob(database_pb_.delegate().blob());
      SecureBlob delegate_secret(database_pb_.delegate().secret());
      action = tpm_->GetEndorsementPublicKeyWithDelegate(
          ek_public_key, delegate_blob, delegate_secret);
      // Warn if we know we will not be able to compute the EID. We can't
      // say no for sure because if the TPM becomes unowned then the EID
      // can be computed, but that will not happen without specific action
      // and the attestation database will not be in a great state then.
      if (!tpm_->IsTransient(action) &&
          !database_pb_.delegate().has_can_read_internal_pub()) {
        // If the results are definitively success or a final failure, record
        // whether the delegate had permission or not.
        database_pb_.mutable_delegate()->set_can_read_internal_pub(
            action == Tpm::kTpmRetryNone);
        PersistDatabaseChanges();
      }
      // Return in case of success or if the caller can retry.
      if (action == Tpm::kTpmRetryNone || tpm_->IsTransient(action)) {
        return action;
      }
      // Fall through in case the TPM has been reset but not the database.
      LOG(WARNING) << "Attestation: Computing the EID is likely to fail."
                   << " Request an enrollment certificate instead.";
    }
  }
  // Always calling this if we fail with the delegate allows us to retrieve
  // the key in situations where we have a database with a delegate but we
  // cleared the owner password later.
  action = tpm_->GetEndorsementPublicKey(ek_public_key);
  if (action != Tpm::kTpmRetryNone) {
    ek_public_key->clear();
  }
  return action;
}

bool Attestation::ComputeEnterpriseEnrollmentId(
    SecureBlob* enterprise_enrollment_id) {
  return !tpm_->IsTransient(ComputeEnterpriseEnrollmentIdInternal(
      enterprise_enrollment_id));
}

Tpm::TpmRetryAction Attestation::ComputeEnterpriseEnrollmentIdInternal(
    brillo::SecureBlob* enterprise_enrollment_id) {
  brillo::SecureBlob den;
  ComputeEnterpriseEnrollmentNonce(&den);
  if (den.empty()) {
    enterprise_enrollment_id->clear();
    return Tpm::kTpmRetryNone;
  }

  SecureBlob ek_public_key;
  Tpm::TpmRetryAction action = GetTpmEndorsementPublicKey(&ek_public_key);
  if (tpm_->IsTransient(action)) {
    return action;
  }
  if (ek_public_key.empty()) {
    enterprise_enrollment_id->clear();
    return action;
  }

  // Extract the modulus from the public key.
  const unsigned char* asn1_ptr =
      reinterpret_cast<const unsigned char*>(ek_public_key.data());
  crypto::ScopedRSA public_key(
      d2i_RSAPublicKey(nullptr, &asn1_ptr, ek_public_key.size()));
  if (!public_key.get()) {
    LOG(ERROR) << "Attestation: Failed to decode public endorsement key.";
    return Tpm::kTpmRetryFailNoRetry;
  }
  brillo::Blob modulus(BN_num_bytes(public_key.get()->n), 0);
  int length = BN_bn2bin(public_key.get()->n, modulus.data());
  if (length <= 0) {
    LOG(ERROR)
        << "Attestation: Failed to extract public endorsement key modulus.";
    return Tpm::kTpmRetryFailNoRetry;
  }
  modulus.resize(length);

  // Compute the EID based on den and modulus.
  *enterprise_enrollment_id = CryptoLib::HmacSha256(den, modulus);
  return Tpm::kTpmRetryNone;
}

void Attestation::RSADeleter::operator()(void* ptr) const {
  if (ptr)
    RSA_free(reinterpret_cast<RSA*>(ptr));
}

void Attestation::X509Deleter::operator()(void* ptr) const {
  if (ptr)
    X509_free(reinterpret_cast<X509*>(ptr));
}

void Attestation::EVP_PKEYDeleter::operator()(void* ptr) const {
  if (ptr)
    EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(ptr));
}

void Attestation::NETSCAPE_SPKIDeleter::operator()(void* ptr) const {
  if (ptr)
    NETSCAPE_SPKI_free(reinterpret_cast<NETSCAPE_SPKI*>(ptr));
}

}  // namespace cryptohome
