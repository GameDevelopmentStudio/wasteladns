#ifndef UNITYBUILD
#include "../platform.h"
#include "core.h"
#include "input.h"
#endif

namespace Platform {
namespace DIRECTX9 {

    LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

        switch (message) {
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            namespace KB = ::Input::Keyboard;
            KB::PollData* keyboardPollData = (KB::PollData*)GetPropA(hWnd, "KeyboardPollData");
            if (keyboardPollData && wParam != VK_CONTROL && wParam != VK_PROCESSKEY) {
                const KB::Keys::Enum key = keyboardPollData->mapping.mapping[HIWORD(lParam) & 0x1FF];
                bool state = ((lParam >> 31) & 1) == 0;
                keyboardPollData->queue.keyStates[key] = state;
            }
        }
        break;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    template <typename _GameData>
    int main(
          HINSTANCE hInstance
        , HINSTANCE hPrevInstance
        , LPSTR lpCmdLine
        , int nCmdShow
    ) {
        (void)lpCmdLine;
        (void)hPrevInstance;

        HWND hWnd;
        Platform::State platform = {};
        {
            WNDCLASSEX wc;
            ZeroMemory(&wc, sizeof(WNDCLASSEX));
            wc.cbSize = sizeof(WNDCLASSEX);
            // Redraw on horizontal or vertical resize
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = hInstance;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = "WindowClass";
            RegisterClassEx(&wc);

            Platform::WindowConfig windowConfig;
            loadLaunchConfig(windowConfig);

            hWnd = CreateWindowEx(
                  NULL
                , "WindowClass"
                , windowConfig.title
                , WS_OVERLAPPEDWINDOW
                , 0, 0, windowConfig.window_width, windowConfig.window_height
                , NULL /*parent*/
                , NULL /*menu*/
                , hInstance
                , NULL
            );
            if (hWnd == NULL) {
                return 0;
            }

            ShowWindow(hWnd, nCmdShow);

            platform.screen.width = windowConfig.window_width;
            platform.screen.height = windowConfig.window_height;
            platform.screen.desiredRatio = platform.screen.width / (f32)platform.screen.height;
            platform.screen.fullscreen = windowConfig.fullscreen;
        }

        u64 frequency;
        if (!QueryPerformanceFrequency((LARGE_INTEGER*)&frequency)) {
            return 0;
        }

        LPDIRECT3D9 d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (d3d == NULL) {
            return 0;
        }

        LPDIRECT3DDEVICE9 d3ddev;
        D3DPRESENT_PARAMETERS d3dpp;
        ZeroMemory(&d3dpp, sizeof(d3dpp));
        d3dpp.Windowed = TRUE; // TODO: support fullscreen
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = hWnd;
        d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
        d3dpp.BackBufferWidth = platform.screen.width;
        d3dpp.BackBufferHeight = platform.screen.height;

        d3d->CreateDevice(
              D3DADAPTER_DEFAULT
            , D3DDEVTYPE_HAL
            , hWnd
            , D3DCREATE_SOFTWARE_VERTEXPROCESSING
            , &d3dpp
            , &d3ddev
        );

        if (d3ddev != NULL) {

            ::Input::Gamepad::State pads[1];
            ::Input::Gamepad::KeyboardMapping keyboardPadMappings[1];
            platform.input.padCount = 1;
            platform.input.pads = pads;
            ::Input::Gamepad::load(keyboardPadMappings[0]);

            ::Input::Keyboard::PollData keyboardPollData;
            ::Input::Keyboard::load(keyboardPollData.mapping);
            keyboardPollData.queue = {};
            SetPropA(hWnd, "KeyboardPollData", &keyboardPollData);

            u64 start;
            QueryPerformanceCounter((LARGE_INTEGER*)&start);

            platform.time.running = 0.0;
            platform.time.now = platform.time.start = start / (f64)frequency;

            _GameData game;
            Platform::GameConfig config;
            Platform::start<_GameData>(game, config, platform);

            // temp scene hack init
            struct CustomVertex {
                enum { fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE };
                FLOAT x, y, z;  // from the D3DFVF_XYZ flag
                DWORD color;    // from the D3DFVF_DIFFUSE flag
            };
            LPDIRECT3DVERTEXBUFFER9 v_buffer = NULL;
            {
                CustomVertex vertices[] = {
                    { 3.0f, 0.0f, -3.0f, D3DCOLOR_XRGB(0, 0, 255), },
                    { 0.0f, 0.0f, 3.0f, D3DCOLOR_XRGB(0, 255, 0), },
                    { -3.0f, 0.0f, -3.0f, D3DCOLOR_XRGB(255, 0, 0), },
                };

                d3ddev->CreateVertexBuffer(
                    3 * sizeof(CustomVertex)
                    , 0 /*Usage*/
                    , CustomVertex::fvf
                    , D3DPOOL_MANAGED
                    , &v_buffer
                    , nullptr /*pSharedHandle*/
                );
                void* pVoid;
                v_buffer->Lock(0, 0, (void**)&pVoid, 0);
                memcpy(pVoid, vertices, sizeof(vertices));
                v_buffer->Unlock();
            }
            d3ddev->SetRenderState(D3DRS_LIGHTING, FALSE);

            MSG msg;

            do {
                if (platform.time.now >= config.nextFrame) {

                    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }

                    // Input
                    if ((config.requestFlags & (Platform::RequestFlags::PollKeyboard)) != 0) {
                        ::Input::Keyboard::pollState(platform.input.keyboard, keyboardPollData.queue);
                    }
                    for (u32 i = 0; i < platform.input.padCount; i++) {
                        ::Input::Gamepad::pollState(platform.input.pads[i], keyboardPollData.queue, keyboardPadMappings[i]);
                    }

                    Platform::update<_GameData>(game, config, platform);

                    // temp scene hack update
                    d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                    d3ddev->BeginScene();
                    {
                        d3ddev->SetTransform(D3DTS_VIEW, (D3DMATRIX*)&game.cameraMgr.activeCam->modelviewMatrix.dataCM);

                        f32 fovDeg = 45.f;
                        f32 aspect = platform.screen.width / (f32)platform.screen.height;
                        f32 near = 1.f;
                        f32 far = 100.f;
                        
                        f32 halfFovRad = fovDeg * 0.5f * Math::d2r<f32>;
                        f32 height = Math::cos(halfFovRad) / Math::sin(halfFovRad);
                        f32 width = height / aspect;
                        f32 range = far / (far - near);

                        // TODO: sort this out properly
                        f32 perspectiveMatrix[16] = {};
                        perspectiveMatrix[0] = width;
                        perspectiveMatrix[5] = height;
                        perspectiveMatrix[10] = range;
                        perspectiveMatrix[11] = 1.f;
                        perspectiveMatrix[14] = -range * near;

                        d3ddev->SetTransform(D3DTS_PROJECTION, (D3DMATRIX*)&perspectiveMatrix);

                        d3ddev->SetFVF(CustomVertex::fvf);
                        d3ddev->SetStreamSource(0, v_buffer, 0, sizeof(CustomVertex));
                        d3ddev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
                    }
                    d3ddev->EndScene();
                    d3ddev->Present(NULL, NULL, NULL, NULL);

                    if (config.quit) {
                        PostQuitMessage(0);
                    }
                }

                u64 now;
                QueryPerformanceCounter((LARGE_INTEGER*)&now);
                platform.time.now = now / (f64)frequency;
                platform.time.running = platform.time.now - platform.time.start;
            } while (msg.message != WM_QUIT);

            RemovePropA(hWnd, "KeyboardPollData");
            d3ddev->Release();
        }
        d3d->Release();

        return 1;
    }
}
}
