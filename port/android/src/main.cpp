#include <android/log.h>
#include <android_native_app_glue.h>

#include <DeviceContext.h>
#include <EngineFactoryVk.h>
#include <NativeWindow.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <SwapChain.h>

#include <exception>

namespace {

constexpr char kLogTag[] = "faf_android";

#define FAF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define FAF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct AppState {
    Diligent::IEngineFactoryVk* factory = nullptr;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;
    bool initialized = false;
};

bool CreateSwapChain(android_app* app, AppState& state) {
    if (!app->window || !state.factory || !state.device || !state.context) {
        return false;
    }

    const int width = ANativeWindow_getWidth(app->window);
    const int height = ANativeWindow_getHeight(app->window);
    if (width <= 0 || height <= 0) {
        return false;
    }

    Diligent::SwapChainDesc desc;
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);

    Diligent::NativeWindow window{};
    window.pAWindow = app->window;
    state.factory->CreateSwapChainVk(state.device, state.context, desc, window, &state.swapChain);
    FAF_LOGI("Vulkan swap chain created (%d x %d)", width, height);
    return state.swapChain != nullptr;
}

bool Initialize(android_app* app, AppState& state) {
    try {
        state.factory = Diligent::LoadAndGetEngineFactoryVk();
        if (!state.factory) {
            FAF_LOGE("Diligent Vulkan factory is unavailable");
            return false;
        }

        Diligent::EngineVkCreateInfo createInfo;
        createInfo.NumImmediateContexts = 1;
        state.factory->CreateDeviceAndContextsVk(createInfo, &state.device, &state.context);
        state.initialized = state.device != nullptr && state.context != nullptr;
        if (!state.initialized) {
            FAF_LOGE("Diligent could not create a Vulkan device/context");
            return false;
        }
        return CreateSwapChain(app, state);
    } catch (const std::exception& e) {
        FAF_LOGE("Graphics initialization failed: %s", e.what());
        return false;
    } catch (...) {
        FAF_LOGE("Graphics initialization failed with an unknown error");
        return false;
    }
}

void Render(AppState& state) {
    if (!state.swapChain) {
        return;
    }

    auto* backBuffer = state.swapChain->GetCurrentBackBufferRTV();
    const float clearColor[] = {0.025f, 0.045f, 0.09f, 1.0f};
    state.context->SetRenderTargets(1, &backBuffer, nullptr,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state.context->ClearRenderTarget(backBuffer, clearColor,
                                     Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state.swapChain->Present();
}

void HandleCommand(android_app* app, int32_t command) {
    auto& state = *static_cast<AppState*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (!state.initialized && !Initialize(app, state)) {
            app->destroyRequested = 1;
        } else if (state.initialized && !state.swapChain && !CreateSwapChain(app, state)) {
            app->destroyRequested = 1;
        }
        break;
    case APP_CMD_TERM_WINDOW:
        state.swapChain.Release();
        break;
    case APP_CMD_LOST_FOCUS:
        state.swapChain.Release();
        break;
    case APP_CMD_GAINED_FOCUS:
        if (state.initialized && !state.swapChain && !CreateSwapChain(app, state)) {
            app->destroyRequested = 1;
        }
        break;
    default:
        break;
    }
}

} // namespace

void android_main(android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = HandleCommand;

    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int timeout = state.swapChain ? 0 : -1;
        if (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 && source) {
            source->process(app, source);
        }
        Render(state);
    }

    state.swapChain.Release();
    state.context.Release();
    state.device.Release();
}
