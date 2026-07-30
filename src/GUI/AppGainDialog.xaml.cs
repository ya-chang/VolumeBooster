using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Windows;

namespace VolumeBooster.GUI
{
    public partial class AppGainDialog : Window
    {
        public uint ProcessId { get; private set; }
        public string AppName { get; private set; } = "";
        public float GainDb { get; private set; }

        public AppGainDialog()
        {
            InitializeComponent();
            LoadRunningApps();
            GainSlider.ValueChanged += GainSlider_ValueChanged;
        }

        private void LoadRunningApps()
        {
            var apps = new List<AppInfo>();
            
            foreach (var proc in Process.GetProcesses())
            {
                try
                {
                    // 只显示有主窗口的进程
                    if (proc.MainWindowHandle != IntPtr.Zero && !string.IsNullOrEmpty(proc.MainWindowTitle))
                    {
                        apps.Add(new AppInfo
                        {
                            ProcessId = (uint)proc.Id,
                            DisplayName = proc.ProcessName,
                            Icon = "📱"
                        });
                    }
                }
                catch
                {
                    // 某些进程无法访问，跳过
                }
            }

            AppListBox.ItemsSource = apps.OrderBy(a => a.DisplayName).ToList();
        }

        private void AppListBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (AppListBox.SelectedItem is AppInfo app)
            {
                ManualProcessName.Text = app.DisplayName;
            }
        }

        private void GainSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (!IsLoaded) return;
            float db = (float)e.NewValue;
            float percent = (float)(Math.Pow(10, db / 20.0) * 100);
            GainLabel.Text = $"+{db:F0} dB ({percent:F0}%)";
        }

        private void Ok_Click(object sender, RoutedEventArgs e)
        {
            // 获取选中的应用
            if (AppListBox.SelectedItem is AppInfo app)
            {
                ProcessId = app.ProcessId;
                AppName = app.DisplayName;
            }
            else if (!string.IsNullOrEmpty(ManualProcessName.Text))
            {
                // 通过进程名查找
                var procs = Process.GetProcessesByName(ManualProcessName.Text);
                if (procs.Length > 0)
                {
                    ProcessId = (uint)procs[0].Id;
                    AppName = ManualProcessName.Text;
                }
                else
                {
                    System.Windows.MessageBox.Show("找不到该进程，请确认进程名正确且正在运行。", 
                                   "错误", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }
            }
            else
            {
                System.Windows.MessageBox.Show("请选择一个应用程序或手动输入进程名。", 
                               "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            GainDb = (float)GainSlider.Value;
            DialogResult = true;
        }

        private void Cancel_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }
    }

    public class AppInfo
    {
        public uint ProcessId { get; set; }
        public string DisplayName { get; set; } = "";
        public string Icon { get; set; } = "📱";
    }
}
