using System;
using System.IO;
using System.Text.Json;

namespace VolumeBooster.GUI
{
    public class Settings
    {
        public float GlobalGain { get; set; } = 100;
        public bool LimiterEnabled { get; set; } = true;
        public int ThresholdIndex { get; set; } = 1;  // -3 dB
        public bool AutoStart { get; set; } = false;
        public bool IsEnabled { get; set; } = true;

        private static readonly string SettingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "VolumeBooster", "settings.json");

        public static Settings Load()
        {
            try
            {
                if (File.Exists(SettingsPath))
                {
                    string json = File.ReadAllText(SettingsPath);
                    return JsonSerializer.Deserialize<Settings>(json) ?? new Settings();
                }
            }
            catch
            {
                // 设置文件损坏，返回默认值
            }
            return new Settings();
        }

        public void Save()
        {
            try
            {
                string? dir = Path.GetDirectoryName(SettingsPath);
                if (dir != null && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                string json = JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(SettingsPath, json);
            }
            catch
            {
                // 保存失败，忽略
            }
        }
    }
}
