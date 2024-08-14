#include "Window.hpp"

#include "Core/Log.hpp"
#include "Core/Numerics.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>


#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_sdl2.h"

static SDL_Window* window = nullptr;

namespace Helix {
    static f32 sdl_get_monitor_refresh() {
        SDL_DisplayMode current;
        int should_be_zero = SDL_GetCurrentDisplayMode(0, &current);
        HASSERT(!should_be_zero);
        return 1.0f / current.refresh_rate;
    }

    void Window::init(void* configuration_) {
        HINFO("WindowService init");

        if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
            HERROR("SDL Init error: {}", SDL_GetError());
            return;
        }

        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(0, &current);

        WindowConfiguration& configuration = *(WindowConfiguration*)configuration_;

        SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

        window = SDL_CreateWindow(configuration.name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, configuration.width, configuration.height, window_flags);

        HINFO("Window created successfully");

        int window_width, window_height;
        SDL_Vulkan_GetDrawableSize(window, &window_width, &window_height);

        width = (u32)window_width;
        height = (u32)window_height;

        // Assing this so it can be accessed from outside.
        platform_handle = window;

        // Callbacks
        os_messages_callbacks.init(configuration.allocator, 4);
        os_messages_callbacks_data.init(configuration.allocator, 4);

        display_refresh = sdl_get_monitor_refresh();
    }

    void Window::shutdown() {

        os_messages_callbacks_data.shutdown();
        os_messages_callbacks.shutdown();

        SDL_DestroyWindow(window);
        SDL_Quit();

        HINFO("WindowService shutdown");
    }

    void Window::handle_os_messages() {

        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type) {
            case SDL_QUIT:
            {
                requested_exit = true;
                goto propagate_event;
                break;
            }

            // Handle subevent
            case SDL_WINDOWEVENT:
            {
                switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                {
                    // Resize only if even numbers are used?
                    // NOTE: This goes in an infinite loop when maximising a window that has an odd width/height.
                    {
                        u32 new_width = (u32)(event.window.data1);
                        u32 new_height = (u32)(event.window.data2);

                        // Update only if needed.
                        if (new_width != width || new_height != height) {
                            resized = true;
                            width = new_width;
                            height = new_height;

                            HTRACE("Resizing to {}, {}", width, height);
                        }
                    }

                    break;
                }

                case SDL_WINDOWEVENT_FOCUS_GAINED:
                {
                    HTRACE("Focus Gained");
                    break;
                }
                case SDL_WINDOWEVENT_FOCUS_LOST:
                {
                    HTRACE("Focus Lost");
                    break;
                }
                case SDL_WINDOWEVENT_MAXIMIZED:
                {
                    HTRACE("Maximized");
                    minimized = false;
                    break;
                }
                case SDL_WINDOWEVENT_MINIMIZED:
                {
                    HTRACE("Minimized");
                    minimized = true;
                    break;
                }
                case SDL_WINDOWEVENT_RESTORED:
                {
                    HTRACE("Restored");
                    minimized = false;
                    break;
                }
                case SDL_WINDOWEVENT_TAKE_FOCUS:
                {
                    HTRACE("Take Focus");
                    break;
                }
                case SDL_WINDOWEVENT_EXPOSED:
                {
                    HTRACE("Exposed");
                    break;
                }

                case SDL_WINDOWEVENT_CLOSE:
                {
                    requested_exit = true;
                    HTRACE("Window close event received.");
                    break;
                }
                default:
                {
                    display_refresh = sdl_get_monitor_refresh();
                    break;
                }
                }
                goto propagate_event;
                break;
            }
            }
            // Maverick: 
        propagate_event:
            // Callbacks
            for (u32 i = 0; i < os_messages_callbacks.size; ++i) {
                OsMessagesCallback callback = os_messages_callbacks[i];
                callback(&event, os_messages_callbacks_data[i]);
            }
        }
    }

    void Window::set_fullscreen(bool value) {
        if (value)
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        else {
            SDL_SetWindowFullscreen(window, 0);
        }
    }

    void Window::register_os_messages_callback(OsMessagesCallback callback, void* user_data) {

        os_messages_callbacks.push(callback);
        os_messages_callbacks_data.push(user_data);
    }

    void Window::unregister_os_messages_callback(OsMessagesCallback callback) {
        HASSERT_MSG(os_messages_callbacks.size < 8, "This array is too big for a linear search. Consider using something different!");

        for (u32 i = 0; i < os_messages_callbacks.size; ++i) {
            if (os_messages_callbacks[i] == callback) {
                os_messages_callbacks.delete_swap(i);
                os_messages_callbacks_data.delete_swap(i);
            }
        }
    }

    void Window::center_mouse(bool dragging) {
        if (dragging) {
            SDL_WarpMouseInWindow(window, Helix::roundu32(width / 2.f), Helix::roundu32(height / 2.f));
            SDL_SetWindowGrab(window, SDL_TRUE);
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }
        else {
            SDL_SetWindowGrab(window, SDL_FALSE);
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
    }

} // namespace Helix