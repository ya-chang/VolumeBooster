using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

namespace VolumeBooster.GUI
{
    public class AppGainEntry
    {
        public uint ProcessId { get; set; }
        public string AppName { get; set; } = "";
        public float GainDb { get; set; }
    }

    public class NamedPipeClient : IDisposable
    {
        private const uint PIPE_MAGIC = 0x564F4C42;  // "VOLB"
        private const uint PIPE_VERSION = 1;

        private readonly List<AppGainEntry> _appGains = new();
        private readonly string _pipeName;

        public NamedPipeClient()
        {
            // 获取当前 session ID
            uint sessionId = GetCurrentSessionId();
            _pipeName = $"\\\\.\\pipe\\VolumeBooster_{sessionId}";
        }

        public void SendGain(float gainDb, float thresholdDb, bool limiterEnabled)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(
                    ".", _pipeName.Substring(4),  // 去掉 \\.\ 前缀
                    PipeDirection.Out,
                    PipeOptions.None,
                    TokenImpersonationLevel.None);

                pipe.Connect(100);  // 100ms 超时

                // 构建消息
                var msg = new PipeMessage
                {
                    Magic = PIPE_MAGIC,
                    Version = PIPE_VERSION,
                    GlobalGainDb = gainDb,
                    ThresholdDb = thresholdDb,
                    LimiterEnabled = limiterEnabled,
                    AppCount = (uint)Math.Min(_appGains.Count, 32)
                };

                // 填充 per-app 增益
                for (int i = 0; i < Math.Min(_appGains.Count, 32); i++)
                {
                    msg.AppGains[i] = new RawAppGainEntry
                    {
                        ProcessId = _appGains[i].ProcessId,
                        GainDb = _appGains[i].GainDb,
                        AppName = _appGains[i].AppName
                    };
                }

                // 序列化并发送
                byte[] data = StructToBytes(msg);
                pipe.Write(data, 0, data.Length);
            }
            catch (TimeoutException)
            {
                // APO 可能未运行，忽略
            }
            catch (Exception)
            {
                // 通信失败，忽略
            }
        }

        public void AddAppGain(uint processId, string appName, float gainDb)
        {
            // 移除已有的同进程条目
            _appGains.RemoveAll(x => x.ProcessId == processId);
            _appGains.Add(new AppGainEntry
            {
                ProcessId = processId,
                AppName = appName,
                GainDb = gainDb
            });
        }

        public void RemoveAppGain(uint processId)
        {
            _appGains.RemoveAll(x => x.ProcessId == processId);
        }

        public IReadOnlyList<AppGainEntry> GetAppGains() => _appGains.AsReadOnly();

        public void Dispose()
        {
            // 清理
        }

        // ========== 辅助方法 ==========

        private static uint GetCurrentSessionId()
        {
            // 获取当前控制台 session ID
            return (uint)System.Diagnostics.Process.GetCurrentProcess().SessionId;
        }

        private static byte[] StructToBytes<T>(T structure) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            byte[] bytes = new byte[size];
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(structure, ptr, false);
                Marshal.Copy(ptr, bytes, 0, size);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
            return bytes;
        }

        // ========== 消息结构（与 APO 端匹配） ==========

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private struct RawAppGainEntry
        {
            public uint ProcessId;
            public float GainDb;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
            public string AppName;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private struct PipeMessage
        {
            public uint Magic;
            public uint Version;
            public float GlobalGainDb;
            public float ThresholdDb;
            [MarshalAs(UnmanagedType.U1)]
            public bool LimiterEnabled;
            public uint AppCount;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
            public RawAppGainEntry[] AppGains;
        }
    }
}
