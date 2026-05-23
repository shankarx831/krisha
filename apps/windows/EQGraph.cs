using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace KrishaSpoke.Windows
{
    /// <summary>
    /// Thread-safe C# wrapper that P/Invokes into the native C++ DSP core library
    /// to retrieve magnitude responses and calculate logarithmic graphs.
    /// Supports the Phase 2 multi-curve acoustics redesign.
    /// </summary>
    public class EQGraph
    {
        private const string DllName = "krisha_apo";

        // ============================================================================
        // P/Invoke Signatures
        // ============================================================================

        [DllImport(DllName, EntryPoint = "krisha_dsp_create", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KrishaDspCreate(uint sampleRate);

        [DllImport(DllName, EntryPoint = "krisha_dsp_destroy", CallingConvention = CallingConvention.Cdecl)]
        private static extern void KrishaDspDestroy(IntPtr engine);

        [DllImport(DllName, EntryPoint = "krisha_dsp_get_magnitude_at_frequency", CallingConvention = CallingConvention.Cdecl)]
        private static extern float KrishaDspGetMagnitudeAtFrequency(IntPtr engine, float frequencyHz, bool leftChannel);

        [DllImport(DllName, EntryPoint = "krisha_dsp_get_harman_target_at_frequency", CallingConvention = CallingConvention.Cdecl)]
        private static extern float KrishaDspGetHarmanTargetAtFrequency(float frequencyHz);

        // ============================================================================
        // Fields & Properties
        // ============================================================================
        
        private readonly IntPtr _activeEngine;
        private readonly IntPtr _targetEngine;
        private readonly object _lock = new object();
        private const int StepsCount = 120;
        private readonly float[] _logFrequencies;

        public float[] HarmanTargetMagnitudes { get; private set; }
        public float[] RawResponseMagnitudes { get; private set; }
        public float[] EqualizerFilterMagnitudes { get; private set; }
        public float[] EqualizedFinalMagnitudes { get; private set; }

        public EQGraph(uint sampleRate = 48000)
        {
            // Instantiate lock-free active and target engines for off-thread calculation
            lock (_lock)
            {
                _activeEngine = KrishaDspCreate(sampleRate);
                _targetEngine = KrishaDspCreate(sampleRate);
            }

            _logFrequencies = new float[StepsCount];
            HarmanTargetMagnitudes = new float[StepsCount];
            RawResponseMagnitudes = new float[StepsCount];
            EqualizerFilterMagnitudes = new float[StepsCount];
            EqualizedFinalMagnitudes = new float[StepsCount];

            PrecomputeLogarithmicSteps();
        }

        ~EQGraph()
        {
            lock (_lock)
            {
                if (_activeEngine != IntPtr.Zero)
                {
                    KrishaDspDestroy(_activeEngine);
                }
                if (_targetEngine != IntPtr.Zero)
                {
                    KrishaDspDestroy(_targetEngine);
                }
            }
        }

        /// <summary>
        /// Analog-style soft-clamping boundaries helper to prevent visual flatlining
        /// </summary>
        private static float SoftClamp(float db)
        {
            float maxVal = 12.0f;
            float minVal = -12.0f;
            if (db > maxVal)
            {
                return maxVal + 2.0f * (float)Math.Tanh((db - maxVal) / 2.0f);
            }
            else if (db < minVal)
            {
                return minVal + 2.0f * (float)Math.Tanh((db - minVal) / 2.0f);
            }
            return db;
        }

        /// <summary>
        /// Maps 120 logarithmic intervals from 20Hz to 20,000Hz.
        /// </summary>
        private void PrecomputeLogarithmicSteps()
        {
            double logMin = Math.Log10(20.0);
            double logMax = Math.Log10(20000.0);
            double step = (logMax - logMin) / (StepsCount - 1);

            for (int i = 0; i < StepsCount; i++)
            {
                _logFrequencies[i] = (float)Math.Pow(10.0, logMin + i * step);
            }
        }

        /// <summary>
        /// Asynchronously evaluates the 120 logarithmic steps on a background queue
        /// yielding four synchronized response curves with analog soft-clamping.
        /// </summary>
        public Task CalculateResponseAsync()
        {
            return Task.Run(() =>
            {
                lock (_lock)
                {
                    if (_activeEngine == IntPtr.Zero || _targetEngine == IntPtr.Zero) return;

                    float[] harmanTemp = new float[StepsCount];
                    float[] rawTemp = new float[StepsCount];
                    float[] eqTemp = new float[StepsCount];
                    float[] finalTemp = new float[StepsCount];

                    for (int i = 0; i < StepsCount; i++)
                    {
                        float freq = _logFrequencies[i];
                        
                        float harmanDb = KrishaDspGetHarmanTargetAtFrequency(freq);
                        float activeDb = KrishaDspGetMagnitudeAtFrequency(_activeEngine, freq, true);
                        float targetEqDb = KrishaDspGetMagnitudeAtFrequency(_targetEngine, freq, true);
                        
                        float rawDb = harmanDb - targetEqDb;
                        float finalDb = rawDb + activeDb;

                        harmanTemp[i] = SoftClamp(harmanDb);
                        rawTemp[i] = SoftClamp(rawDb);
                        eqTemp[i] = SoftClamp(activeDb);
                        finalTemp[i] = SoftClamp(finalDb);
                    }

                    // Atomic swap to avoid UI thread race conditions
                    HarmanTargetMagnitudes = harmanTemp;
                    RawResponseMagnitudes = rawTemp;
                    EqualizerFilterMagnitudes = eqTemp;
                    EqualizedFinalMagnitudes = finalTemp;
                }
            });
        }
    }
}
