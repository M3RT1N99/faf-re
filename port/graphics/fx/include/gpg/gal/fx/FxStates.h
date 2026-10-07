#pragma once

// The D3D9 effect states an fx_2_0 pass or sampler_state block can assign:
// their names (case-insensitive, the D3D enum names without prefix), the
// D3D9 enum they set, how D3DX turns the assigned value into the DWORD it
// passes to SetRenderState / SetSamplerState / SetTextureStageState, and the
// value names each state accepts.
//
// Numbers are D3D9's (d3d9types.h). The value conversions were measured
// against d3dx9_43 with the legacy compiler (port/graphics/fx/README.md):
// a float given to a DWORD state is truncated (AlphaRef = 0.5 sets 0,
// MaxAnisotropy = 4.7 sets 4), an integer given to a float state is
// converted (DepthBias = 1 sets 1.0f), a float4 given to a colour state
// becomes a D3DCOLOR (BorderColor = float4(1,0.5,0,1) sets 0xFFFF8000), and
// bool states keep integers as written (AlphaTestEnable = 3 sets 3).

#include <cstdint>
#include <optional>
#include <string_view>

namespace gpg::gal::fx {

  enum class StateOp : std::uint8_t
  {
    Render, // IDirect3DDevice9::SetRenderState(id, value)
    TextureStage, // SetTextureStageState(index, id, value)
    Sampler, // SetSamplerState(index, id, value)
    Texture, // SetTexture(index, texture parameter) from `Texture[n] = <T>` in a pass
    VertexShader,
    PixelShader
  };

  enum class StateValueKind : std::uint8_t
  {
    Dword, // integers as written, floats truncated
    Bool, // like Dword; true/false/TRUE/FALSE
    Float, // the float's bit pattern
    Color, // a D3DCOLOR; a float4 is converted
    Enum // Dword plus the state's value names, combinable with |
  };

  struct StateEnumValue
  {
    const char* name;
    std::uint32_t value;
  };

  struct StateInfo
  {
    const char* name;
    StateOp op;
    std::uint32_t id;
    StateValueKind kind;
    const StateEnumValue* values; // null-terminated list, or nullptr
  };

  /// Finds a pass state by name (case-insensitive). Render, texture stage and
  /// sampler states, plus Texture, VertexShader and PixelShader.
  [[nodiscard]] const StateInfo* FindPassState(std::string_view name);

  /// Finds a sampler_state state (case-insensitive): the sampler states and
  /// Texture.
  [[nodiscard]] const StateInfo* FindSamplerState(std::string_view name);

  /// The value of one of `state`'s value names (case-insensitive).
  [[nodiscard]] std::optional<std::uint32_t> FindStateValue(const StateInfo& state, std::string_view name);

  /// Upper-case name of a D3D9 state id for reports ("ALPHABLENDENABLE").
  [[nodiscard]] const char* StateName(StateOp op, std::uint32_t id);

} // namespace gpg::gal::fx
