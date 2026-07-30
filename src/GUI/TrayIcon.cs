using System;
using System.Drawing;
using System.Windows;
using System.Windows.Forms;

namespace VolumeBooster.GUI
{
    public class TrayIcon : IDisposable
    {
        private readonly NotifyIcon _trayIcon;
        private readonly MainWindow _mainWindow;

        public TrayIcon(MainWindow mainWindow)
        {
            _mainWindow = mainWindow;

            _trayIcon = new NotifyIcon
            {
                Icon = SystemIcons.Application,  // TODO: 替换为自定义图标
                Text = "系统音量增强器",
                Visible = true
            };

            var contextMenu = new ContextMenuStrip();
            contextMenu.Items.Add("打开主窗口", null, (s, e) => ShowWindow());
            contextMenu.Items.Add("增强 +10%", null, (s, e) => AdjustGain(10));
            contextMenu.Items.Add("增强 -10%", null, (s, e) => AdjustGain(-10));
            contextMenu.Items.Add("-");
            contextMenu.Items.Add("恢复原声", null, (s, e) => DisableBoost());
            contextMenu.Items.Add("增强开启", null, (s, e) => EnableBoost());
            contextMenu.Items.Add("-");
            contextMenu.Items.Add("退出", null, (s, e) => ExitApp());

            _trayIcon.ContextMenuStrip = contextMenu;
            _trayIcon.DoubleClick += (s, e) => ShowWindow();
        }

        private void ShowWindow()
        {
            _mainWindow.Show();
            _mainWindow.WindowState = WindowState.Normal;
            _mainWindow.Activate();
        }

        private void AdjustGain(int delta)
        {
            _mainWindow.GlobalGainSlider.Value = Math.Max(100,
                Math.Min(500, _mainWindow.GlobalGainSlider.Value + delta));
        }

        private void EnableBoost()
        {
            _mainWindow.BtnEnable_Click(null!, null!);
        }

        private void DisableBoost()
        {
            _mainWindow.BtnDisable_Click(null!, null!);
        }

        private void ExitApp()
        {
            _trayIcon.Visible = false;
            System.Windows.Application.Current.Shutdown();
        }

        public void Dispose()
        {
            _trayIcon.Dispose();
        }
    }
}
