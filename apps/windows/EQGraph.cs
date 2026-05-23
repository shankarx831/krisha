using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace KrishaSpoke.Windows
{
    /// <summary>
    /// Thread-safe C# wrapper that P/Invokes into the native C++ DSP core library
    /// to retrieve magnitude responses and calculate logarithmic graphs.
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
        
        private readonly IntPtr _tempEngine;
        private readonly object _lock = new object();
        private const int StepsCount = 120;
        private readonly float[] _logFrequencies;

        public float[] LeftMagnitudes { get; private set; }
        public float[] RightMagnitudes { get; private set; }
        public float[] HarmanTargetMagnitudes { get; private set; }

        public EQGraph(uint sampleRate = 48000)
        {
            // Create a transient DSP engine for off-thread graph evaluation
            lock (_lock)
            {
                _tempEngine = KrishaDspCreate(sampleRate);
            }

            _logFrequencies = new float[StepsCount];
            LeftMagnitudes = new float[StepsCount];
            RightMagnitudes = new float[StepsCount];
            HarmanTargetMagnitudes = new float[StepsCount];

            PrecomputeLogarithmicSteps();
        }

        ~EQGraph()
        {
            lock (_lock)
            {
                if (_tempEngine != IntPtr.Zero)
                {
                    KrishaDspDestroy(_tempEngine);
                }
            }
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
        /// to guarantee the main rendering thread is never blocked.
        /// </summary>
        public Task CalculateResponseAsync()
        {
            return Task.Run(() =>
            {
                lock (_lock)
                {
                    if (_tempEngine == IntPtr.Zero) return;

                    float[] leftTemp = new float[StepsCount];
                    float[] rightTemp = new float[StepsCount];
                    float[] harmanTemp = new float[StepsCount];

                    for (int i = 0; i < StepsCount; i++)
                    {
                        float freq = _logFrequencies[i];
                        leftTemp[i] = KrishaDspGetMagnitudeAtFrequency(_tempEngine, freq, true);
                        rightTemp[i] = KrishaDspGetMagnitudeAtFrequency(_tempEngine, freq, false);
                        harmanTemp[i] = KrishaDspGetHarmanTargetAtFrequency(freq);
                    }

                    // Atomic swap to avoid UI thread race conditions
                    LeftMagnitudes = leftTemp;
                    RightMagnitudes = rightTemp;
                    HarmanTargetMagnitudes = harmanTemp;
                }
            });
        }
    }
}
