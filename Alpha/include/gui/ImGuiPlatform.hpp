// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - ImGui Platform Layer
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// PURPOSE: Platform abstraction for Dear ImGui (Windows/Linux)
//
// BACKENDS SUPPORTED:
// - Win32 + OpenGL3 (Windows native)
// - GLFW + OpenGL3 (Linux/WSL)
//
// DEPENDENCIES:
// - Dear ImGui (bundled or system)
// - OpenGL 3.3+
// - GLFW3 (Linux) or Win32 API (Windows)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#ifdef ALPHA_GUI_ENABLED

#include <string>
#include <functional>
#include <atomic>

// Platform detection
#ifdef _WIN32
    #define ALPHA_PLATFORM_WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <GL/gl.h>
#else
    #define ALPHA_PLATFORM_GLFW
    #include <GLFW/glfw3.h>
#endif

// ImGui includes (header-only mode for simplicity)
// In production, link against compiled imgui library
#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    #define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

namespace Alpha {
namespace GUI {

// ═══════════════════════════════════════════════════════════════════════════════
// PLATFORM CONTEXT
// ═══════════════════════════════════════════════════════════════════════════════
struct PlatformContext {
    int width = 1600;
    int height = 1000;
    std::string title = "Alpha Trading System";
    bool vsync = true;
    
#ifdef ALPHA_PLATFORM_WIN32
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
#else
    GLFWwindow* window = nullptr;
#endif
    
    std::atomic<bool> running{false};
    std::atomic<bool> minimized{false};
};

// ═══════════════════════════════════════════════════════════════════════════════
// PLATFORM INITIALIZATION (GLFW VERSION)
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef ALPHA_PLATFORM_GLFW

inline bool platform_init(PlatformContext& ctx) {
    if (!glfwInit()) {
        return false;
    }
    
    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    
    ctx.window = glfwCreateWindow(ctx.width, ctx.height, ctx.title.c_str(), nullptr, nullptr);
    if (!ctx.window) {
        glfwTerminate();
        return false;
    }
    
    glfwMakeContextCurrent(ctx.window);
    glfwSwapInterval(ctx.vsync ? 1 : 0);
    
    ctx.running.store(true);
    return true;
}

inline void platform_shutdown(PlatformContext& ctx) {
    if (ctx.window) {
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
    }
    glfwTerminate();
    ctx.running.store(false);
}

inline bool platform_poll_events(PlatformContext& ctx) {
    if (!ctx.window) return false;
    
    glfwPollEvents();
    
    if (glfwWindowShouldClose(ctx.window)) {
        ctx.running.store(false);
        return false;
    }
    
    // Check if minimized
    int w, h;
    glfwGetFramebufferSize(ctx.window, &w, &h);
    ctx.minimized.store(w == 0 || h == 0);
    ctx.width = w;
    ctx.height = h;
    
    return true;
}

inline void platform_begin_frame(PlatformContext& ctx) {
    if (ctx.minimized.load()) return;
    glViewport(0, 0, ctx.width, ctx.height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

inline void platform_end_frame(PlatformContext& ctx) {
    if (ctx.minimized.load()) return;
    glfwSwapBuffers(ctx.window);
}

#endif // ALPHA_PLATFORM_GLFW

// ═══════════════════════════════════════════════════════════════════════════════
// PLATFORM INITIALIZATION (WIN32 VERSION)
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef ALPHA_PLATFORM_WIN32

LRESULT CALLBACK AlphaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

inline bool platform_init(PlatformContext& ctx) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = AlphaWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "AlphaGUIWindow";
    
    if (!RegisterClassEx(&wc)) {
        return false;
    }
    
    ctx.hwnd = CreateWindowEx(
        0,
        "AlphaGUIWindow",
        ctx.title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ctx.width, ctx.height,
        nullptr, nullptr,
        wc.hInstance,
        &ctx
    );
    
    if (!ctx.hwnd) {
        return false;
    }
    
    ctx.hdc = GetDC(ctx.hwnd);
    
    // Setup pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    
    int format = ChoosePixelFormat(ctx.hdc, &pfd);
    SetPixelFormat(ctx.hdc, format, &pfd);
    
    ctx.hglrc = wglCreateContext(ctx.hdc);
    wglMakeCurrent(ctx.hdc, ctx.hglrc);
    
    ShowWindow(ctx.hwnd, SW_SHOW);
    UpdateWindow(ctx.hwnd);
    
    ctx.running.store(true);
    return true;
}

inline void platform_shutdown(PlatformContext& ctx) {
    if (ctx.hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(ctx.hglrc);
        ctx.hglrc = nullptr;
    }
    if (ctx.hdc && ctx.hwnd) {
        ReleaseDC(ctx.hwnd, ctx.hdc);
        ctx.hdc = nullptr;
    }
    if (ctx.hwnd) {
        DestroyWindow(ctx.hwnd);
        ctx.hwnd = nullptr;
    }
    UnregisterClass("AlphaGUIWindow", GetModuleHandle(nullptr));
    ctx.running.store(false);
}

inline bool platform_poll_events(PlatformContext& ctx) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ctx.running.store(false);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return ctx.running.load();
}

inline void platform_begin_frame(PlatformContext& ctx) {
    if (ctx.minimized.load()) return;
    
    RECT rect;
    GetClientRect(ctx.hwnd, &rect);
    ctx.width = rect.right - rect.left;
    ctx.height = rect.bottom - rect.top;
    
    glViewport(0, 0, ctx.width, ctx.height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

inline void platform_end_frame(PlatformContext& ctx) {
    if (ctx.minimized.load()) return;
    SwapBuffers(ctx.hdc);
}

inline LRESULT CALLBACK AlphaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PlatformContext* ctx = reinterpret_cast<PlatformContext*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_SIZE:
            if (ctx) {
                ctx->minimized.store(wParam == SIZE_MINIMIZED);
            }
            return 0;
        case WM_CLOSE:
            if (ctx) {
                ctx->running.store(false);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

#endif // ALPHA_PLATFORM_WIN32

}  // namespace GUI
}  // namespace Alpha

#endif // ALPHA_GUI_ENABLED
