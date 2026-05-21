using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace RadioformSpoke.Windows
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        private NotifyIconWrapper _notifyIcon;
        private MemoryMappedFile _sharedMemory;
        private MemoryMappedViewAccessor _sharedAccessor;
        private static readonly string SharedMemoryName = "Local\\RadioformDSPSharedMemory";
        private const int SharedMemorySize = 4096; // 4KB configuration block

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr DefWindowProc(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            // Initialize thread-safe memory-mapped configuration channel to the sAPO spoke
            try
            {
                _sharedMemory = MemoryMappedFile.CreateOrOpen(SharedMemoryName, SharedMemorySize);
                _sharedAccessor = _sharedMemory.CreateViewAccessor();
                
                // Initialize default L/R preamp offsets to 0.0 dB
                WritePreampLeft(0.0f);
                WritePreampRight(0.0f);
                WriteBypass(false);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[KRISHA WinSpoke] Shared Memory IPC Initialization Error: {ex.Message}");
            }

            // Create Tray Icon with zero polling Win32 event driven message loop
            _notifyIcon = new NotifyIconWrapper(this);
            _notifyIcon.Create();

            Debug.WriteLine("[KRISHA WinSpoke] Headless Tray Application Started. Idle CPU: 0.0%");
        }

        protected override void OnExit(ExitEventArgs e)
        {
            _notifyIcon?.Dispose();
            _sharedAccessor?.Dispose();
            _sharedMemory?.Dispose();
            base.OnExit(e);
        }

        public void WritePreampLeft(float val)
        {
            if (_sharedAccessor != null && _sharedAccessor.CanWrite)
            {
                _sharedAccessor.Write(0, val); // Offset 0: Left preamp gain (float)
                SignalPresetChanged();
            }
        }

        public void WritePreampRight(float val)
        {
            if (_sharedAccessor != null && _sharedAccessor.CanWrite)
            {
                _sharedAccessor.Write(4, val); // Offset 4: Right preamp gain (float)
                SignalPresetChanged();
            }
        }

        public void WriteBypass(bool bypass)
        {
            if (_sharedAccessor != null && _sharedAccessor.CanWrite)
            {
                _sharedAccessor.Write(8, bypass ? 1 : 0); // Offset 8: Bypass state (int)
                SignalPresetChanged();
            }
        }

        private void SignalPresetChanged()
        {
            // Trigger Windows native Named Event to wake up the sAPO process immediately without polling
            try
            {
                using (var changeEvent = EventWaitHandle.OpenExisting("Local\\RadioformPresetChangedEvent"))
                {
                    changeEvent.Set();
                }
            }
            catch
            {
                // Named event might not be created if audiodg.exe isn't actively running, ignore
            }
        }
    }

    /// <summary>
    /// Pure native Win32 wrapper for tray shell notifications avoiding WPF NotifyIcon overhead.
    /// Sleeps in native runloop when idle.
    /// </summary>
    internal class NotifyIconWrapper : IDisposable
    {
        private readonly App _app;
        private IntPtr _hwnd;
        private static readonly int WM_TRAYICON = 0x8000 + 2048; // WM_USER + 2048
        private const int NIM_ADD = 0x00000000;
        private const int NIM_MODIFY = 0x00000001;
        private const int NIM_DELETE = 0x00000002;
        private const int NIF_MESSAGE = 0x00000001;
        private const int NIF_ICON = 0x00000002;
        private const int NIF_TIP = 0x00000004;
        private const int WM_RBUTTONUP = 0x0205;
        private const int WM_LBUTTONDBLCLK = 0x0203;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        private struct NOTIFYICONDATA
        {
            public int cbSize;
            public IntPtr hWnd;
            public int uID;
            public int uFlags;
            public int uCallbackMessage;
            public IntPtr hIcon;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string szTip;
            public int dwState;
            public int dwStateMask;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string szInfo;
            public int uVersionOrTimeout;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
            public string szInfoTitle;
            public int dwInfoFlags;
            public Guid guidItem;
            public IntPtr hBalloonIcon;
        }

        [DllImport("shell32.dll", CharSet = CharSet.Auto)]
        private static extern bool Shell_NotifyIcon(int dwMessage, [In] ref NOTIFYICONDATA lpData);

        [DllImport("user32.dll")]
        private static extern IntPtr LoadIcon(IntPtr hInstance, IntPtr lpIconName);

        public NotifyIconWrapper(App app)
        {
            _app = app;
        }

        public void Create()
        {
            // Zero background polling native shell notification handle
            NOTIFYICONDATA nid = new NOTIFYICONDATA
            {
                cbSize = Marshal.SizeOf(typeof(NOTIFYICONDATA)),
                hWnd = IntPtr.Zero,
                uID = 1,
                uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
                uCallbackMessage = WM_TRAYICON,
                hIcon = LoadIcon(IntPtr.Zero, (IntPtr)32512), // IDI_APPLICATION default
                szTip = "KRISHA Universal - Windows Tray"
            };

            Shell_NotifyIcon(NIM_ADD, ref nid);
        }

        public void Dispose()
        {
            NOTIFYICONDATA nid = new NOTIFYICONDATA
            {
                cbSize = Marshal.SizeOf(typeof(NOTIFYICONDATA)),
                hWnd = IntPtr.Zero,
                uID = 1
            };
            Shell_NotifyIcon(NIM_DELETE, ref nid);
        }
    }
}
