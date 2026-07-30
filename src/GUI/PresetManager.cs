using System.Collections.Generic;

namespace VolumeBooster.GUI
{
    public class Preset
    {
        public string Name { get; set; } = "";
        public float GlobalGain { get; set; } = 100;
        public bool LimiterEnabled { get; set; } = true;
        public int ThresholdDb { get; set; } = -3;
    }

    public class PresetManager
    {
        private readonly Dictionary<string, Preset> _presets = new()
        {
            ["Music"] = new Preset
            {
                Name = "音乐",
                GlobalGain = 130,
                LimiterEnabled = true,
                ThresholdDb = -3
            },
            ["WeChat"] = new Preset
            {
                Name = "微信通话",
                GlobalGain = 160,
                LimiterEnabled = true,
                ThresholdDb = -3
            },
            ["Movie"] = new Preset
            {
                Name = "电影",
                GlobalGain = 190,
                LimiterEnabled = true,
                ThresholdDb = -6
            },
            ["Game"] = new Preset
            {
                Name = "游戏",
                GlobalGain = 150,
                LimiterEnabled = true,
                ThresholdDb = -3
            }
        };

        public Preset? GetPreset(string name)
        {
            return _presets.TryGetValue(name, out var preset) ? preset : null;
        }

        public IReadOnlyDictionary<string, Preset> GetAllPresets() => _presets;
    }
}
