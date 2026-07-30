using System.Windows;

namespace VolumeBooster.GUI
{
    public partial class App : System.Windows.Application
    {
        private TrayIcon? _trayIcon;

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            // 确保只有一个实例运行
            bool createdNew;
            using var mutex = new System.Threading.Mutex(true, "VolumeBooster_SingleInstance", out createdNew);
            if (!createdNew)
            {
                MessageBox.Show("程序已在运行中，请检查系统托盘。", "音量增强器", MessageBoxButton.OK, MessageBoxImage.Information);
                Shutdown();
                return;
            }

            // 创建主窗口并显示
            var mainWindow = new MainWindow();
            mainWindow.Show();

            // 初始化系统托盘
            _trayIcon = new TrayIcon(mainWindow);
        }

        protected override void OnExit(ExitEventArgs e)
        {
            _trayIcon?.Dispose();
            base.OnExit(e);
        }
    }
}
