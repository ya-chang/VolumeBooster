using System;
using System.Windows;
using System.Windows.Threading;

namespace VolumeBooster.GUI
{
    public partial class MainWindow : Window
    {
        private readonly NamedPipeClient _pipeClient;
        private readonly SharedMemoryReader _sharedMemory;
        private readonly HotkeyManager _hotkeyManager;
        private readonly DispatcherTimer _levelTimer;
        private readonly PresetManager _presetManager;
        private bool _isEnabled = true;

        public MainWindow()
        {
            InitializeComponent();

            _pipeClient = new NamedPipeClient();
            _sharedMemory = new SharedMemoryReader();
            _hotkeyManager = new HotkeyManager(this);
            _presetManager = new PresetManager();

            // 注册全局快捷键
            _hotkeyManager.RegisterHotkeys();
            _hotkeyManager.OnVolumeUp += () => AdjustGain(10);
            _hotkeyManager.OnVolumeDown += () => AdjustGain(-10);

            // 电平刷新定时器（50ms = 20fps）
            _levelTimer = new DispatcherTimer();
            _levelTimer.Interval = TimeSpan.FromMilliseconds(50);
            _levelTimer.Tick += LevelTimer_Tick;
            _levelTimer.Start();

            // 加载设置
            LoadSettings();
        }

        private void LoadSettings()
        {
            var settings = Settings.Load();
            GlobalGainSlider.Value = settings.GlobalGain;
            LimiterEnabled.IsChecked = settings.LimiterEnabled;
            ThresholdComboBox.SelectedIndex = settings.ThresholdIndex;
            AutoStartEnabled.IsChecked = settings.AutoStart;
            _isEnabled = settings.IsEnabled;

            UpdateGainLabel(settings.GlobalGain);
            SendParameters();
        }

        private void SaveSettings()
        {
            var settings = new Settings
            {
                GlobalGain = (float)GlobalGainSlider.Value,
                LimiterEnabled = LimiterEnabled.IsChecked == true,
                ThresholdIndex = ThresholdComboBox.SelectedIndex,
                AutoStart = AutoStartEnabled.IsChecked == true,
                IsEnabled = _isEnabled
            };
            settings.Save();
        }

        // ========== 全局增益 ==========

        private void GlobalGainSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (!IsLoaded) return;
            float gain = (float)e.NewValue;
            UpdateGainLabel(gain);
            SendParameters();
            SaveSettings();
        }

        private void UpdateGainLabel(float gain)
        {
            GlobalGainLabel.Text = $"{gain:F0}%";
        }

        private void AdjustGain(int delta)
        {
            double newValue = GlobalGainSlider.Value + delta;
            newValue = Math.Max(GlobalGainSlider.Minimum, Math.Min(GlobalGainSlider.Maximum, newValue));
            GlobalGainSlider.Value = newValue;
        }

        // ========== 限幅器 ==========

        private void Limiter_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsLoaded) return;
            SendParameters();
            SaveSettings();
        }

        private void Threshold_Changed(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (!IsLoaded) return;
            SendParameters();
            SaveSettings();
        }

        // ========== 预设 ==========

        private void PresetMusic_Click(object sender, RoutedEventArgs e) => ApplyPreset("Music");
        private void PresetWeChat_Click(object sender, RoutedEventArgs e) => ApplyPreset("WeChat");
        private void PresetMovie_Click(object sender, RoutedEventArgs e) => ApplyPreset("Movie");
        private void PresetGame_Click(object sender, RoutedEventArgs e) => ApplyPreset("Game");

        private void ApplyPreset(string presetName)
        {
            var preset = _presetManager.GetPreset(presetName);
            if (preset != null)
            {
                GlobalGainSlider.Value = preset.GlobalGain;
                LimiterEnabled.IsChecked = preset.LimiterEnabled;
                // 更新阈值下拉框
                for (int i = 0; i < ThresholdComboBox.Items.Count; i++)
                {
                    var item = ThresholdComboBox.Items[i] as System.Windows.Controls.ComboBoxItem;
                    if (item?.Tag?.ToString() == preset.ThresholdDb.ToString())
                    {
                        ThresholdComboBox.SelectedIndex = i;
                        break;
                    }
                }
            }
        }

        // ========== Per-App 增益 ==========

        private void AddAppGain_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new AppGainDialog();
            if (dialog.ShowDialog() == true)
            {
                // 添加到列表并更新
                _pipeClient.AddAppGain(dialog.ProcessId, dialog.AppName, dialog.GainDb);
                RefreshAppGainList();
                SendParameters();
            }
        }

        private void RefreshAppGainList()
        {
            AppGainList.Items.Clear();
            foreach (var entry in _pipeClient.GetAppGains())
            {
                AppGainList.Items.Add($"{entry.AppName} — {entry.GainDb:F0} dB");
            }
        }

        // ========== 开机自启 ==========

        private void AutoStart_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsLoaded) return;
            bool enabled = AutoStartEnabled.IsChecked == true;
            SetAutoStart(enabled);
            SaveSettings();
        }

        private void SetAutoStart(bool enabled)
        {
            try
            {
                var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\CurrentVersion\Run", true);
                if (key != null)
                {
                    if (enabled)
                    {
                        key.SetValue("VolumeBooster", System.Reflection.Assembly.GetExecutingAssembly().Location);
                    }
                    else
                    {
                        key.DeleteValue("VolumeBooster", false);
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"设置开机自启失败: {ex.Message}", "错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        // ========== 增强开关 ==========

        private void BtnEnable_Click(object sender, RoutedEventArgs e)
        {
            _isEnabled = true;
            SendParameters();
            BtnEnable.IsEnabled = false;
            BtnDisable.IsEnabled = true;
            SaveSettings();
        }

        private void BtnDisable_Click(object sender, RoutedEventArgs e)
        {
            _isEnabled = false;
            // 发送 100% 增益（原声）
            _pipeClient.SendGain(0.0f, -3.0f, true);
            BtnEnable.IsEnabled = true;
            BtnDisable.IsEnabled = false;
            SaveSettings();
        }

        // ========== 发送参数到 APO ==========

        private void SendParameters()
        {
            if (!_isEnabled) return;

            float gainPercent = (float)GlobalGainSlider.Value;
            float gainDb = (float)(20.0 * Math.Log10(gainPercent / 100.0));
            float thresholdDb = -3.0f;

            if (ThresholdComboBox.SelectedItem is System.Windows.Controls.ComboBoxItem item &&
                item.Tag is string tag && float.TryParse(tag, out float t))
            {
                thresholdDb = t;
            }

            bool limiterOn = LimiterEnabled.IsChecked == true;
            _pipeClient.SendGain(gainDb, thresholdDb, limiterOn);
        }

        // ========== 电平刷新 ==========

        private void LevelTimer_Tick(object sender, EventArgs e)
        {
            var level = _sharedMemory.ReadLevel();
            if (level != null)
            {
                LevelLeft.Value = Math.Max(-100, Math.Min(0, level.RmsLeft));
                LevelRight.Value = Math.Max(-100, Math.Min(0, level.RmsRight));
                LevelLeftLabel.Text = $"{level.RmsLeft:F1} dB";
                LevelRightLabel.Text = $"{level.RmsRight:F1} dB";
            }
        }

        // ========== 窗口事件 ==========

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            // 最小化到托盘而不是关闭
            e.Cancel = true;
            Hide();
        }

        protected override void OnClosed(EventArgs e)
        {
            _hotkeyManager?.Dispose();
            _levelTimer?.Stop();
            base.OnClosed(e);
        }
    }
}
