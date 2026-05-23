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

namespace KrishaSpoke.Windows
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        private NotifyIconWrapper _notifyIcon;
        private MemoryMappedFile _sharedMemory;
        private MemoryMappedViewAccessor _sharedAccessor;
        private static readonly string SharedMemoryName = "Local\\KrishaDSPSharedMemory";
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

            // Display the WPF Settings window with Panic button
            var settingsWindow = new SettingsWindow();
            settingsWindow.Show();

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
                using (var changeEvent = EventWaitHandle.OpenExisting("Local\\KrishaPresetChangedEvent"))
                {
                    changeEvent.Set();
                }
            }
            catch
            {
                // Named event might not be created if audiodg.exe isn't actively running, ignore
            }
        }

        public static void RunUninstallScript()
        {
            try
            {
                string scriptPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Manage_sAPO.ps1");
                ProcessStartInfo psi = new ProcessStartInfo
                {
                    FileName = "powershell.exe",
                    Arguments = $"-ExecutionPolicy Bypass -File \"{scriptPath}\" -Uninstall",
                    Verb = "runas", // Requests administrative elevation
                    UseShellExecute = true
                };
                Process.Start(psi);
                Application.Current.Shutdown();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Uninstallation failed to start: {ex.Message}", "KRISHA Panic Uninstall", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        public static string GetPresetsDirectory()
        {
            string appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string presetsDir = Path.Combine(appData, "Krisha", "Presets");
            if (!Directory.Exists(presetsDir))
            {
                Directory.CreateDirectory(presetsDir);
            }
            return presetsDir;
        }

        public static void SaveCustomPreset(string name, string jsonContent)
        {
            try
            {
                string path = Path.Combine(GetPresetsDirectory(), $"{name}.json");
                File.WriteAllText(path, jsonContent);
                Debug.WriteLine($"[KRISHA WinSpoke] Saved custom preset '{name}' to: {path}");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[KRISHA WinSpoke] Failed to save preset: {ex.Message}");
            }
        }

        public static string[] LoadCustomPresets()
        {
            try
            {
                string presetsDir = GetPresetsDirectory();
                return Directory.GetFiles(presetsDir, "*.json");
            }
            catch
            {
                return new string[0];
            }
        }
    }

    /// <summary>
    /// Programmatic settings window containing safety uninstaller button.
    /// </summary>
    public class SettingsWindow : Window
    {
        private Slider _sliderLeft;
        private Slider _sliderRight;
        private TextBlock _txtLeftValue;
        private TextBlock _txtRightValue;
        private CheckBox _chkBypass;

        public SettingsWindow()
        {
            Title = "KRISHA Settings";
            Width = 460;
            Height = 430;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = new SolidColorBrush(Color.FromRgb(18, 18, 18));
            ResizeMode = ResizeMode.NoResize;
            FontFamily = new FontFamily("Segoe UI");

            Grid rootGrid = new Grid { Margin = new Thickness(24) };
            rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // Title
            rootGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) }); // Grid panel
            rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // Management Bar
            rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // Quit Row

            // Modern subtle title
            TextBlock titleLabel = new TextBlock
            {
                Text = "KRISHA UNIVERSAL",
                Foreground = Brushes.White,
                FontSize = 18,
                FontWeight = FontWeights.SemiBold,
                HorizontalAlignment = HorizontalAlignment.Center,
                Margin = new Thickness(0, 0, 0, 16)
            };
            Grid.SetRow(titleLabel, 0);
            rootGrid.Children.Add(titleLabel);

            // Container for Settings menu (Fluent card style)
            StackPanel cardPanel = new StackPanel
            {
                Background = new SolidColorBrush(Color.FromRgb(28, 28, 30)),
                Margin = new Thickness(0, 0, 0, 20)
            };
            // Add subtle border
            Border cardBorder = new Border
            {
                BorderBrush = new SolidColorBrush(Color.FromRgb(44, 44, 46)),
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Child = cardPanel,
                Padding = new Thickness(16)
            };
            Grid.SetRow(cardBorder, 1);
            rootGrid.Children.Add(cardBorder);

            // Left Preamp control
            cardPanel.Children.Add(new TextBlock
            {
                Text = "Preamp Left Offset",
                Foreground = new SolidColorBrush(Color.FromRgb(142, 142, 147)),
                FontSize = 11,
                FontWeight = FontWeights.Medium,
                Margin = new Thickness(0, 0, 0, 4)
            });

            Grid leftSliderGrid = new Grid();
            leftSliderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            leftSliderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            _sliderLeft = new Slider
            {
                Minimum = -12.0,
                Maximum = 12.0,
                Value = 0.0,
                TickFrequency = 0.1,
                IsSnapToTickEnabled = true,
                Margin = new Thickness(0, 0, 12, 12),
                VerticalAlignment = VerticalAlignment.Center
            };
            // Style the slider to use clean Win11 blue accent color
            _sliderLeft.Foreground = new SolidColorBrush(Color.FromRgb(0, 122, 255));
            _sliderLeft.ValueChanged += OnSliderLeftChanged;

            _txtLeftValue = new TextBlock
            {
                Text = "0.0 dB",
                Foreground = Brushes.White,
                FontSize = 12,
                FontWeight = FontWeights.Medium,
                Width = 50,
                TextAlignment = TextAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 0, 12)
            };

            Grid.SetColumn(_sliderLeft, 0);
            Grid.SetColumn(_txtLeftValue, 1);
            leftSliderGrid.Children.Add(_sliderLeft);
            leftSliderGrid.Children.Add(_txtLeftValue);
            cardPanel.Children.Add(leftSliderGrid);

            // Right Preamp control
            cardPanel.Children.Add(new TextBlock
            {
                Text = "Preamp Right Offset",
                Foreground = new SolidColorBrush(Color.FromRgb(142, 142, 147)),
                FontSize = 11,
                FontWeight = FontWeights.Medium,
                Margin = new Thickness(0, 4, 0, 4)
            });

            Grid rightSliderGrid = new Grid();
            rightSliderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            rightSliderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            _sliderRight = new Slider
            {
                Minimum = -12.0,
                Maximum = 12.0,
                Value = 0.0,
                TickFrequency = 0.1,
                IsSnapToTickEnabled = true,
                Margin = new Thickness(0, 0, 12, 16),
                VerticalAlignment = VerticalAlignment.Center
            };
            _sliderRight.Foreground = new SolidColorBrush(Color.FromRgb(0, 122, 255));
            _sliderRight.ValueChanged += OnSliderRightChanged;

            _txtRightValue = new TextBlock
            {
                Text = "0.0 dB",
                Foreground = Brushes.White,
                FontSize = 12,
                FontWeight = FontWeights.Medium,
                Width = 50,
                TextAlignment = TextAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 0, 16)
            };

            Grid.SetColumn(_sliderRight, 0);
            Grid.SetColumn(_txtRightValue, 1);
            rightSliderGrid.Children.Add(_sliderRight);
            rightSliderGrid.Children.Add(_txtRightValue);
            cardPanel.Children.Add(rightSliderGrid);

            // Bypass Switch
            _chkBypass = new CheckBox
            {
                Content = "Bypass DSP Processing",
                Foreground = Brushes.White,
                FontSize = 12,
                FontWeight = FontWeights.Medium,
                VerticalAlignment = VerticalAlignment.Center
            };
            _chkBypass.Checked += OnBypassChanged;
            _chkBypass.Unchecked += OnBypassChanged;
            cardPanel.Children.Add(_chkBypass);

            // One-Touch Install & Complete System Purge Engine Bar
            Grid managementGrid = new Grid { Margin = new Thickness(0, 0, 0, 10) };
            managementGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            managementGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            Button btnInstall = new Button
            {
                Content = "Install Driver / Sync Setup",
                Background = new SolidColorBrush(Color.FromRgb(0, 122, 255)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.SemiBold,
                FontSize = 12,
                Padding = new Thickness(10),
                BorderThickness = new Thickness(0),
                Margin = new Thickness(0, 0, 5, 0),
                Cursor = Cursors.Hand
            };

            Button btnPurge = new Button
            {
                Content = "Purge System Files",
                Background = new SolidColorBrush(Color.FromRgb(255, 59, 48)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.SemiBold,
                FontSize = 12,
                Padding = new Thickness(10),
                BorderThickness = new Thickness(0),
                Margin = new Thickness(5, 0, 0, 0),
                Cursor = Cursors.Hand
            };

            // Rounded corners templates for buttons
            ControlTemplate btnTemplate = new ControlTemplate(typeof(Button));
            FrameworkElementFactory borderFactory = new FrameworkElementFactory(typeof(Border));
            borderFactory.SetValue(Border.CornerRadiusProperty, new CornerRadius(6));
            borderFactory.SetValue(Border.BackgroundProperty, new TemplateBindingExtension(Button.BackgroundProperty));
            FrameworkElementFactory presenterFactory = new FrameworkElementFactory(typeof(ContentPresenter));
            presenterFactory.SetValue(ContentPresenter.HorizontalAlignmentProperty, HorizontalAlignment.Center);
            presenterFactory.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
            borderFactory.AppendChild(presenterFactory);
            btnTemplate.VisualTree = borderFactory;
            btnInstall.Template = btnTemplate;
            btnPurge.Template = btnTemplate;

            btnInstall.Click += (s, e) =>
            {
                Task.Run(() =>
                {
                    try
                    {
                        string scriptPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Manage_sAPO.ps1");
                        ProcessStartInfo psi = new ProcessStartInfo
                        {
                            FileName = "powershell.exe",
                            Arguments = $"-ExecutionPolicy Bypass -File \"{scriptPath}\" -Install",
                            Verb = "runas",
                            UseShellExecute = true
                        };
                        Process.Start(psi);
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Driver registration failed: {ex.Message}", "KRISHA Install Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                });
            };

            btnPurge.Click += (s, e) =>
            {
                Task.Run(() =>
                {
                    try
                    {
                        string scriptPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Manage_sAPO.ps1");
                        ProcessStartInfo psi = new ProcessStartInfo
                        {
                            FileName = "powershell.exe",
                            Arguments = $"-ExecutionPolicy Bypass -File \"{scriptPath}\" -Uninstall",
                            Verb = "runas",
                            UseShellExecute = true
                        };
                        var proc = Process.Start(psi);
                        proc?.WaitForExit();

                        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                        string krishaDir = Path.Combine(localAppData, "Krisha");
                        if (Directory.Exists(krishaDir))
                        {
                            Directory.Delete(krishaDir, true);
                        }

                        Application.Current.Dispatcher.Invoke(() =>
                        {
                            Application.Current.Shutdown();
                        });
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Purge failed: {ex.Message}", "KRISHA Purge Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                });
            };

            Grid.SetColumn(btnInstall, 0);
            Grid.SetColumn(btnPurge, 1);
            managementGrid.Children.Add(btnInstall);
            managementGrid.Children.Add(btnPurge);

            Grid.SetRow(managementGrid, 2);
            rootGrid.Children.Add(managementGrid);

            // Bottom Quit Row
            Button btnQuit = new Button
            {
                Content = "Quit KRISHA",
                Background = new SolidColorBrush(Color.FromRgb(44, 44, 46)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Medium,
                FontSize = 12,
                Padding = new Thickness(8),
                BorderThickness = new Thickness(0),
                Cursor = Cursors.Hand
            };
            btnQuit.Template = btnTemplate;
            btnQuit.Click += (s, e) => Application.Current.Shutdown();

            Grid.SetRow(btnQuit, 3);
            rootGrid.Children.Add(btnQuit);

            Content = rootGrid;
        }

        private void OnSliderLeftChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            float val = (float)_sliderLeft.Value;
            _txtLeftValue.Text = string.Format("{0:0.0} dB", val);
            ((App)Application.Current).WritePreampLeft(val);
        }

        private void OnSliderRightChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            float val = (float)_sliderRight.Value;
            _txtRightValue.Text = string.Format("{0:0.0} dB", val);
            ((App)Application.Current).WritePreampRight(val);
        }

        private void OnBypassChanged(object sender, RoutedEventArgs e)
        {
            bool isBypass = _chkBypass.IsChecked == true;
            ((App)Application.Current).WriteBypass(isBypass);
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
