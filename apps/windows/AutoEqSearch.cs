using System;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace RadioformSpoke.Windows
{
    /// <summary>
    /// Struct layouts matching C++ radioform_types.h for zero-copy memory interoperability.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct RadioformBand
    {
        public float FrequencyHz;
        public float GainDb;
        public float QFactor;
        public int FilterType;
        [MarshalAs(UnmanagedType.U1)]
        public bool Enabled;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct RadioformPreset
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 10)]
        public RadioformBand[] Bands;
        public uint NumBands;
        public float PreampDb;
        public float PreampLeftDb;
        public float PreampRightDb;
        [MarshalAs(UnmanagedType.U1)]
        public bool LimiterEnabled;
        public float LimiterThresholdDb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Name;
    }

    public class AutoEqSearch
    {
        private const string DllName = "radioform_apo";
        private readonly HttpClient _httpClient;

        [DllImport(DllName, EntryPoint = "radioform_preset_parse_autoeq", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RadioformPresetParseAutoeq(
            [MarshalAs(UnmanagedType.LPStr)] string text, 
            ref RadioformPreset preset
        );

        public AutoEqSearch()
        {
            _httpClient = new HttpClient();
            _httpClient.DefaultRequestHeaders.Add("User-Agent", "KRISHAUniversal-WinSpoke");
        }

        /// <summary>
        /// Asynchronously fetches the ParametricEQ.txt for the specified headphone model
        /// and parses it using our native C++ core parser.
        /// </summary>
        public async Task<RadioformPreset?> DownloadAndParsePresetAsync(string headphonePathName)
        {
            try
            {
                // Construct URL matching jaakkopasanen/AutoEq master tree path
                string url = $"https://raw.githubusercontent.com/jaakkopasanen/AutoEq/master/results/{headphonePathName}/ParametricEQ.txt";
                
                string content = await _httpClient.GetStringAsync(url);
                if (string.IsNullOrWhiteSpace(content)) return null;

                // Allocate a preset on the managed stack and pass reference to native C++ parser
                RadioformPreset preset = new RadioformPreset
                {
                    Bands = new RadioformBand[10]
                };

                int result = RadioformPresetParseAutoeq(content, ref preset);
                if (result == 0) // RADIOFORM_OK
                {
                    return preset;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[KRISHA WinSpoke] AutoEq Search/Parse error: {ex.Message}");
            }
            return null;
        }
    }
}
