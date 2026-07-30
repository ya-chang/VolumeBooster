using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

namespace VolumeBooster.GUI
{
    public class LevelData
    {
        public float RmsLeft { get; set; }
        public float RmsRight { get; set; }
        public float PeakLeft { get; set; }
        public float PeakRight { get; set; }
        public ulong Timestamp { get; set; }
    }

    public class SharedMemoryReader : IDisposable
    {
        private const string SHM_NAME_PREFIX = "VolumeBooster_LevelBuffer_";
        private const int RING_BUFFER_SIZE = 4096;

        private MemoryMappedFile? _mmf;
        private MemoryMappedViewAccessor? _accessor;
        private uint _lastWriteIndex = 0;

        public SharedMemoryReader()
        {
            try
            {
                uint sessionId = (uint)System.Diagnostics.Process.GetCurrentProcess().SessionId;
                string shmName = $"{SHM_NAME_PREFIX}{sessionId}";
                _mmf = MemoryMappedFile.OpenExisting(shmName, MemoryMappedFileRights.Read);
                _accessor = _mmf.CreateViewAccessor(0, 4096 * 32 + 8, MemoryMappedFileAccess.Read);
            }
            catch
            {
                // 共享内存可能还未创建（APO 未加载）
            }
        }

        public LevelData? ReadLevel()
        {
            if (_accessor == null) return null;

            try
            {
                // 读取 writeIndex（偏移 0）
                uint writeIndex = _accessor.ReadUInt32(0);

                // 如果没有新数据，返回 null
                if (writeIndex == _lastWriteIndex) return null;

                // 读取最新的样本（writeIndex - 1）
                uint sampleIndex = (writeIndex - 1) % RING_BUFFER_SIZE;
                long offset = 8 + sampleIndex * 32;  // 8 字节头部 + 每个样本 32 字节

                var level = new LevelData
                {
                    RmsLeft = _accessor.ReadSingle(offset),
                    RmsRight = _accessor.ReadSingle(offset + 4),
                    PeakLeft = _accessor.ReadSingle(offset + 8),
                    PeakRight = _accessor.ReadSingle(offset + 12),
                    Timestamp = _accessor.ReadUInt64(offset + 16)
                };

                _lastWriteIndex = writeIndex;
                return level;
            }
            catch
            {
                return null;
            }
        }

        public void Dispose()
        {
            _accessor?.Dispose();
            _mmf?.Dispose();
        }
    }
}
