#include "gpg/gal/fx/FxJson.h"

#include <cstdio>

namespace gpg::gal::fx {

  void JsonWriter::Newline()
  {
    mText += '\n';
    mText.append(mFirst.size(), ' ');
  }

  void JsonWriter::BeforeValue()
  {
    if (mAfterKey) {
      mAfterKey = false;
      return;
    }
    if (!mFirst.empty()) {
      if (!mFirst.back()) {
        mText += ',';
      }
      mFirst.back() = false;
      Newline();
    }
  }

  void JsonWriter::BeginObject()
  {
    BeforeValue();
    mText += '{';
    mFirst.push_back(true);
  }

  void JsonWriter::EndObject()
  {
    const bool empty = mFirst.back();
    mFirst.pop_back();
    if (!empty) {
      Newline();
    }
    mText += '}';
  }

  void JsonWriter::BeginArray()
  {
    BeforeValue();
    mText += '[';
    mFirst.push_back(true);
  }

  void JsonWriter::EndArray()
  {
    const bool empty = mFirst.back();
    mFirst.pop_back();
    if (!empty) {
      Newline();
    }
    mText += ']';
  }

  void JsonWriter::Key(const std::string_view key)
  {
    BeforeValue();
    WriteString(key);
    mText += ": ";
    mAfterKey = true;
  }

  void JsonWriter::String(const std::string_view value)
  {
    BeforeValue();
    WriteString(value);
  }

  void JsonWriter::WriteString(const std::string_view value)
  {
    std::string text = "\"";
    for (const char c : value) {
      const auto u = static_cast<unsigned char>(c);
      switch (c) {
      case '"': text += "\\\""; break;
      case '\\': text += "\\\\"; break;
      case '\n': text += "\\n"; break;
      case '\r': text += "\\r"; break;
      case '\t': text += "\\t"; break;
      default:
        if (u < 0x20 || u >= 0x7F) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04x", u);
          text += escape;
        } else {
          text += c;
        }
      }
    }
    text += '"';
    mText += text;
  }

  void JsonWriter::Null()
  {
    BeforeValue();
    mText += "null";
  }

  void JsonWriter::Bool(const bool value)
  {
    BeforeValue();
    mText += value ? "true" : "false";
  }

  void JsonWriter::Number(const std::int64_t value)
  {
    BeforeValue();
    mText += std::to_string(value);
  }

  std::string JsonWriter::Take()
  {
    mText += '\n';
    return std::move(mText);
  }

  std::string WordsToHex(const std::vector<std::uint32_t>& words)
  {
    std::string text;
    char buffer[16];
    for (std::size_t i = 0; i < words.size(); ++i) {
      std::snprintf(buffer, sizeof(buffer), "%s%08x", i ? " " : "", static_cast<unsigned>(words[i]));
      text += buffer;
    }
    return text;
  }

  const char* StateOpName(const StateOp op)
  {
    switch (op) {
    case StateOp::Render: return "render";
    case StateOp::TextureStage: return "stage";
    case StateOp::Sampler: return "sampler";
    case StateOp::Texture: return "texture";
    case StateOp::VertexShader: return "vertexshader";
    case StateOp::PixelShader: return "pixelshader";
    }
    return "?";
  }

  namespace {

    void WriteDescFields(JsonWriter& json, const ParameterDesc& desc)
    {
      json.Key("name");
      json.String(desc.name);
      json.Key("semantic");
      if (desc.semantic.empty()) {
        json.Null();
      } else {
        json.String(desc.semantic);
      }
      json.Key("class");
      json.Number(static_cast<std::int64_t>(desc.parameterClass));
      json.Key("type");
      json.Number(static_cast<std::int64_t>(desc.type));
      json.Key("rows");
      json.Number(desc.rows);
      json.Key("columns");
      json.Number(desc.columns);
      json.Key("elements");
      json.Number(desc.elements);
      json.Key("structMembers");
      json.Number(desc.structMembers);
      json.Key("flags");
      json.Number(desc.flags);
      json.Key("bytes");
      json.Number(desc.bytes);
    }

    void WriteAnnotations(JsonWriter& json, const std::vector<AnnotationInfo>& annotations)
    {
      json.Key("annotations");
      json.BeginArray();
      for (const AnnotationInfo& annotation : annotations) {
        json.BeginObject();
        WriteDescFields(json, annotation.desc);
        if (annotation.desc.type == ParameterType::String) {
          json.Key("string");
          json.String(annotation.string);
        } else {
          json.Key("value");
          json.String(WordsToHex(annotation.value));
        }
        json.EndObject();
      }
      json.EndArray();
    }

    void WriteShader(JsonWriter& json, const char* key, const ShaderEntry& shader)
    {
      if (shader.kind == ShaderEntry::Kind::Unassigned) {
        return;
      }
      json.Key(key);
      if (shader.kind == ShaderEntry::Kind::Null) {
        json.Null();
        return;
      }
      json.BeginObject();
      json.Key("version");
      json.String(VersionTokenName(shader.versionToken));
      if (shader.kind == ShaderEntry::Kind::Asm) {
        json.Key("entry");
        json.String(""); // an asm block has no entry point (D3DX: no debug info)
        json.Key("asm");
        json.Bool(true);
      } else {
        json.Key("entry");
        json.String(shader.entry);
        json.Key("profile");
        json.String(shader.profile);
        json.Key("arguments");
        json.BeginArray();
        for (const std::string& argument : shader.arguments) {
          json.String(argument);
        }
        json.EndArray();
      }
      json.EndObject();
    }

  } // namespace

  std::string EffectMetadataToJson(const EffectMetadata& metadata)
  {
    JsonWriter json;
    json.BeginObject();
    json.Key("schema");
    json.String("faf-fx-metadata/1");
    json.Key("producer");
    json.String("fxmeta");

    json.Key("parameters");
    json.BeginArray();
    for (const ParameterInfo& parameter : metadata.parameters) {
      json.BeginObject();
      WriteDescFields(json, parameter.desc);
      WriteAnnotations(json, parameter.annotations);
      if (parameter.desc.parameterClass != ParameterClass::Object) {
        json.Key("value");
        json.String(WordsToHex(parameter.value));
      }
      if (parameter.desc.type == ParameterType::String) {
        json.Key("string");
        json.String(parameter.string);
      }
      if (parameter.desc.parameterClass == ParameterClass::Struct) {
        json.Key("members");
        json.BeginArray();
        for (const ParameterDesc& member : parameter.members) {
          json.BeginObject();
          WriteDescFields(json, member);
          json.EndObject();
        }
        json.EndArray();
      }
      if (parameter.hasSamplerState) {
        json.Key("sampler");
        json.BeginObject();
        json.Key("texture");
        if (parameter.samplerTexture.empty()) {
          json.Null();
        } else {
          json.String(parameter.samplerTexture);
        }
        json.Key("states");
        json.BeginArray();
        for (const SamplerStateValue& state : parameter.samplerStates) {
          json.BeginArray();
          json.Number(state.state);
          json.Number(state.value);
          json.EndArray();
        }
        json.EndArray();
        json.EndObject();
      }
      json.EndObject();
    }
    json.EndArray();

    const std::vector<DeviceProfile>& profiles = StandardDeviceProfiles();
    json.Key("techniques");
    json.BeginArray();
    for (const TechniqueInfo& technique : metadata.techniques) {
      json.BeginObject();
      json.Key("name");
      if (technique.named) {
        json.String(technique.name);
      } else {
        json.Null();
      }
      WriteAnnotations(json, technique.annotations);
      json.Key("valid");
      json.BeginObject();
      for (const DeviceProfile& profile : profiles) {
        json.Key(profile.name);
        json.Bool(IsTechniqueValid(technique, profile));
      }
      json.EndObject();
      json.Key("passes");
      json.BeginArray();
      for (const PassInfo& pass : technique.passes) {
        json.BeginObject();
        json.Key("name");
        if (pass.named) {
          json.String(pass.name);
        } else {
          json.Null();
        }
        WriteAnnotations(json, pass.annotations);
        json.Key("states");
        json.BeginArray();
        for (const PassState& state : pass.states) {
          json.BeginObject();
          json.Key("op");
          json.String(StateOpName(state.op));
          json.Key("index");
          json.Number(state.index);
          json.Key("state");
          json.Number(state.state);
          if (state.op == StateOp::Texture) {
            json.Key("texture");
            if (state.texture.empty()) {
              json.Null();
            } else {
              json.String(state.texture);
            }
          } else {
            json.Key("value");
            json.Number(state.value);
          }
          if (state.dynamic) {
            json.Key("dynamic");
            json.Bool(true);
          }
          json.EndObject();
        }
        json.EndArray();
        WriteShader(json, "vertexShader", pass.vertexShader);
        WriteShader(json, "pixelShader", pass.pixelShader);
        json.EndObject();
      }
      json.EndArray();
      json.EndObject();
    }
    json.EndArray();

    json.Key("validTechniques");
    json.BeginObject();
    for (const DeviceProfile& profile : profiles) {
      json.Key(profile.name);
      json.BeginArray();
      for (const TechniqueInfo& technique : metadata.techniques) {
        if (!IsTechniqueValid(technique, profile)) {
          continue;
        }
        if (technique.named) {
          json.String(technique.name);
        } else {
          json.Null();
        }
      }
      json.EndArray();
    }
    json.EndObject();
    json.EndObject();
    return json.Take();
  }

} // namespace gpg::gal::fx
