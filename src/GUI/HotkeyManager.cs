using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace VolumeBooster.GUI
{
    public class HotkeyManager : IDisposable
    {
        private const int HOTKEY_ID_UP = 9001;
        private const int HOTKEY_ID_DOWN = 9002;
        private const uint MOD_CTRL = 0x0002;
        private const uint MOD_ALT = 0x0001;
        private const uint VK_UP = 0x26;
        private const uint VK_DOWN = 0x28;
        private const int WM_HOTKEY = 0x0312;

        public event Action? OnVolumeUp;
        public event Action? OnVolumeDown;

        private readonly Window _window;
        private HwndSource? _source;
        private bool _registered = false;

        public HotkeyManager(Window window)
        {
            _window = window;
        }

        public void RegisterHotkeys()
        {
            var helper = new WindowInteropHelper(_window);
            
            // 确保窗口句柄已创建
            if (helper.Handle == IntPtr.Zero)
            {
                // 窗口句柄未创建，延迟注册
                _window.SourceInitialized += (s, e) => DoRegister();
            }
            else
            {
                DoRegister();
            }
        }

        private void DoRegister()
        {
            var helper = new WindowInteropHelper(_window);
            _source = HwndSource.FromHwnd(helper.Handle);
            _source?.AddHook(HwndHook);

            // Ctrl+Alt+Up
            bool up = RegisterHotKey(helper.Handle, HOTKEY_ID_UP, MOD_CTRL | MOD_ALT, VK_UP);
            // Ctrl+Alt+Down
            bool down = RegisterHotKey(helper.Handle, HOTKEY_ID_DOWN, MOD_CTRL | MOD_ALT, VK_DOWN);
            
            _registered = up || down;
        }

        private IntPtr HwndHook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == WM_HOTKEY)
            {
                int id = wParam.ToInt32();
                if (id == HOTKEY_ID_UP)
                {
                    OnVolumeUp?.Invoke();
                    handled = true;
                }
                else if (id == HOTKEY_ID_DOWN)
                {
                    OnVolumeDown?.Invoke();
                    handled = true;
                }
            }
            return IntPtr.Zero;
        }

        public void Dispose()
        {
            if (_registered)
            {
                var helper = new WindowInteropHelper(_window);
                UnregisterHotKey(helper.Handle, HOTKEY_ID_UP);
                UnregisterHotKey(helper.Handle, HOTKEY_ID_DOWN);
                _source?.RemoveHook(HwndHook);
                _registered = false;
            }
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    }
}
