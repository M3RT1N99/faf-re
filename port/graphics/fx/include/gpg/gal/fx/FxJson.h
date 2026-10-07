#pragma once

// JSON for effect metadata.
//
// fxmeta (the front end) and fxd3dx_dump (D3DX reflection, Windows only)
// write the same schema, "faf-fx-metadata/1", so the gate can compare them
// field by field:
//
//   parameters[]: name, semantic, class, type, rows, columns, elements,
//                 structMembers, flags, bytes (D3DXPARAMETER_DESC numbering),
//                 annotations[], value (GetValue words as hex), string,
//                 members[] (struct members), sampler {texture, states[[state, value]...]}
//   techniques[]: name, annotations[], valid {profile: bool},
//                 passes[]: name, annotations[], states[] {op, index, state, value, texture},
//                           vertexShader / pixelShader: absent (not assigned), null, or
//                           {version, entry} (+ profile, arguments from fxmeta)
//   validTechniques {profile: [names in FindNextValidTechnique order]}
//
// JsonWriter only formats; it holds no effect semantics, so the D3DX dumper
// can use it without depending on the front end's logic.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpg/gal/fx/FxMetadata.h"

namespace gpg::gal::fx {

  class JsonWriter
  {
  public:
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();
    void Key(std::string_view key);
    void String(std::string_view value);
    void Null();
    void Bool(bool value);
    void Number(std::int64_t value);

    /// The document, ending with a newline.
    [[nodiscard]] std::string Take();

  private:
    void BeforeValue();
    void Newline();
    void WriteString(std::string_view value);

    std::string mText;
    std::vector<bool> mFirst; // per open container: no element written yet
    bool mAfterKey = false;
  };

  /// "3f800000 00000001" - 8 hex digits per word.
  [[nodiscard]] std::string WordsToHex(const std::vector<std::uint32_t>& words);

  /// "render", "stage", "sampler", "texture".
  [[nodiscard]] const char* StateOpName(StateOp op);

  [[nodiscard]] std::string EffectMetadataToJson(const EffectMetadata& metadata);

} // namespace gpg::gal::fx
