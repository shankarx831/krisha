/**
4:  * @file main.cpp
5:  * @brief Linux GTK4 and Cairo System Tray Spoke wrapping the frozen C++ DSP Core.
6:  */
7: 
8: #include <gtk/gtk.h>
9: #include <cairo.h>
10: #include <cmath>
11: #include <vector>
12: #include <string>
13: #include "krisha_dsp.h"
14: 
15: static krisha_dsp_engine_t* g_dsp_engine = nullptr;
16: static GtkWidget* g_graph_drawing_area = nullptr;
17: static float g_preamp_left = 0.0f;
18: static float g_preamp_right = 0.0f;
19: static bool g_bypass = false;
20: 
21: // 120 Logarithmic steps calculation
22: static const int STEPS_COUNT = 120;
23: static std::vector<float> g_frequencies(STEPS_COUNT);
24: 
25: static void precompute_logarithmic_steps() {
26:     double log_min = std::log10(20.0);
27:     double log_max = std::log10(20000.0);
28:     double step = (log_max - log_min) / (STEPS_COUNT - 1);
29: 
30:     for (int i = 0; i < STEPS_COUNT; i++) {
31:         g_frequencies[i] = std::pow(10.0, log_min + i * step);
32:     }
33: }
34: 
35: // Cairo drawing callback for high-performance logarithmic response plotting
36: static void on_draw_cairo(GtkDrawingArea* drawing_area, cairo_t* cr, int width, int height, gpointer user_data) {
37:     // Clear background with premium slate gray (#121214)
38:     cairo_set_source_rgb(cr, 0.07, 0.07, 0.08);
39:     cairo_paint(cr);
40: 
41:     // Draw grid lines
42:     cairo_set_line_width(cr, 1.0);
43:     cairo_set_source_rgba(cr, 0.2, 0.2, 0.25, 0.4);
44: 
45:     // Frequencies grid (20Hz, 100Hz, 1kHz, 10kHz, 20kHz)
46:     std::vector<float> grid_freqs = {20, 100, 1000, 10000, 20000};
47:     double log_min = std::log10(20.0);
48:     double log_max = std::log10(20000.0);
49: 
50:     for (float f : grid_freqs) {
51:         double x_ratio = (std::log10(f) - log_min) / (log_max - log_min);
52:         double x = x_ratio * width;
53:         cairo_move_to(cr, x, 0);
54:         cairo_line_to(cr, x, height);
55:         cairo_stroke(cr);
56:     }
57: 
58:     // Decibel grid (+12dB, 0dB, -12dB)
59:     std::vector<float> grid_dbs = {12.0f, 0.0f, -12.0f};
60:     for (float db : grid_dbs) {
61:         double y_ratio = 1.0 - (db + 12.0) / 24.0;
62:         double y = y_ratio * height;
63:         cairo_move_to(cr, 0, y);
64:         cairo_line_to(cr, width, y);
65:         cairo_stroke(cr);
66:     }
67: 
68:     if (!g_dsp_engine || g_bypass) return;
69: 
70:     // RENDER LEFT CHANNEL - Neon Cyan (#00E5E5)
71:     cairo_set_line_width(cr, 2.5);
72:     cairo_set_source_rgb(cr, 0.0, 0.9, 0.9);
73:     bool first = true;
74:     for (int i = 0; i < STEPS_COUNT; i++) {
75:         double x_ratio = (double)i / (STEPS_COUNT - 1);
76:         double x = x_ratio * width;
77: 
78:         float gain_db = krisha_dsp_get_magnitude_at_frequency(g_dsp_engine, g_frequencies[i], true);
79:         // Map -12dB to +12dB onto height
80:         double y_ratio = 1.0 - (gain_db + 12.0) / 24.0;
81:         y_ratio = std::max(0.0, std::min(1.0, y_ratio));
82:         double y = y_ratio * height;
83: 
84:         if (first) {
85:             cairo_move_to(cr, x, y);
86:             first = false;
87:         } else {
88:             cairo_line_to(cr, x, y);
89:         }
90:     }
91:     cairo_stroke(cr);
92: 
93:     // RENDER RIGHT CHANNEL - Neon Magenta (#FF0099)
94:     cairo_set_source_rgb(cr, 1.0, 0.0, 0.6);
95:     first = true;
96:     for (int i = 0; i < STEPS_COUNT; i++) {
97:         double x_ratio = (double)i / (STEPS_COUNT - 1);
98:         double x = x_ratio * width;
99: 
100:         float gain_db = krisha_dsp_get_magnitude_at_frequency(g_dsp_engine, g_frequencies[i], false);
101:         double y_ratio = 1.0 - (gain_db + 12.0) / 24.0;
102:         y_ratio = std::max(0.0, std::min(1.0, y_ratio));
103:         double y = y_ratio * height;
104: 
105:         if (first) {
106:             cairo_move_to(cr, x, y);
107:             first = false;
108:         } else {
109:             cairo_line_to(cr, x, y);
110:         }
111:     }
112:     cairo_stroke(cr);
113: }
114: 
115: // Slider update callbacks
116: static void on_preamp_left_changed(GtkRange* range, gpointer user_data) {
117:     g_preamp_left = gtk_range_get_value(range);
118:     if (g_dsp_engine) {
119:         krisha_dsp_update_preamp_left(g_dsp_engine, g_preamp_left);
120:     }
121:     gtk_widget_queue_draw(g_graph_drawing_area);
122: }
123: 
124: static void on_preamp_right_changed(GtkRange* range, gpointer user_data) {
125:     g_preamp_right = gtk_range_get_value(range);
126:     if (g_dsp_engine) {
127:         krisha_dsp_update_preamp_right(g_dsp_engine, g_preamp_right);
128:     }
129:     gtk_widget_queue_draw(g_graph_drawing_area);
130: }
131: 
132: static void on_bypass_toggled(GtkToggleButton* button, gpointer user_data) {
133:     g_bypass = gtk_toggle_button_get_active(button);
134:     if (g_dsp_engine) {
135:         krisha_dsp_set_bypass(g_dsp_engine, g_bypass);
136:     }
137:     gtk_widget_queue_draw(g_graph_drawing_area);
138: }
139: 
140: static void activate(GtkApplication* app, gpointer user_data) {
141:     GtkWidget* window = gtk_application_window_new(app);
142:     gtk_window_set_title(GTK_WINDOW(window), "KRISHA Universal - Linux Spoke UI");
143:     gtk_window_set_default_size(GTK_WINDOW(window), 550, 420);
144: 
145:     // Modern dark-themed container layout
146:     GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
147:     gtk_widget_set_margin_all(main_box, 20);
148:     gtk_window_set_child(GTK_WINDOW(window), main_box);
149: 
150:     // Sleek title HUD
151:     GtkWidget* title_label = gtk_label_new(nullptr);
152:     gtk_label_set_markup(GTK_LABEL(title_label), "<span weight='bold' size='x-large' foreground='#00E5E5'>KRISHA</span> <span size='x-large' foreground='#FFFFFF'>Universal</span>");
153:     gtk_box_append(GTK_BOX(main_box), title_label);
154: 
155:     // Cairo Live Graph
156:     g_graph_drawing_area = gtk_drawing_area_new();
157:     gtk_widget_set_size_request(g_graph_drawing_area, -1, 180);
158:     gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_graph_drawing_area), on_draw_cairo, nullptr, nullptr);
159:     gtk_box_append(GTK_BOX(main_box), g_graph_drawing_area);
160: 
161:     // Preamp Balancer panel (Zero polling reactive sliders)
162:     GtkWidget* sliders_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
163:     gtk_box_append(GTK_BOX(main_box), sliders_box);
164: 
165:     // Left Preamp Offset
166:     GtkWidget* left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
167:     gtk_box_append(GTK_BOX(left_box), gtk_label_new("Preamp Left (dB)"));
168:     GtkWidget* left_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 12.0, 0.1);
169:     gtk_range_set_value(GTK_RANGE(left_slider), 0.0);
170:     g_signal_connect(left_slider, "value-changed", G_CALLBACK(on_preamp_left_changed), nullptr);
171:     gtk_box_append(GTK_BOX(left_box), left_slider);
172:     gtk_box_append(GTK_BOX(sliders_box), left_box);
173: 
174:     // Right Preamp Offset
175:     GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
176:     gtk_box_append(GTK_BOX(right_box), gtk_label_new("Preamp Right (dB)"));
177:     GtkWidget* right_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 12.0, 0.1);
178:     gtk_range_set_value(GTK_RANGE(right_slider), 0.0);
179:     g_signal_connect(right_slider, "value-changed", G_CALLBACK(on_preamp_right_changed), nullptr);
180:     gtk_box_append(GTK_BOX(right_box), right_slider);
181:     gtk_box_append(GTK_BOX(sliders_box), right_box);
182: 
183:     // Bypass controls
184:     GtkWidget* bypass_btn = gtk_check_button_new_with_label("Bypass DSP Engine");
185:     g_signal_connect(bypass_btn, "toggled", G_CALLBACK(on_bypass_toggled), nullptr);
186:     gtk_box_append(GTK_BOX(main_box), bypass_btn);
186: 
186:     // Destructive uninstaller panic button
186:     GtkWidget* uninstall_btn = gtk_button_new_with_label("Uninstall Audio Driver (Panic Button)");
186:     gtk_widget_add_css_class(uninstall_btn, "destructive");
186:     g_signal_connect(uninstall_btn, "clicked", G_CALLBACK([](GtkButton* btn, gpointer d) {
186:         int r1 = system("systemctl --user stop krisha.service");
186:         int r2 = system("systemctl --user disable krisha.service");
186:         int r3 = system("rm -f ~/.config/systemd/user/krisha.service");
186:         int r4 = system("rm -f /usr/share/applications/krisha.desktop");
186:         exit(0);
186:     }), nullptr);
186:     gtk_box_append(GTK_BOX(main_box), uninstall_btn);
187: 
188:     // Wakeup logic for GMainContext to keep main execution entirely suspended when idle
189:     GMainContext* ctx = g_main_context_default();
190:     g_main_context_wakeup(ctx);
191: 
192:     gtk_window_present(GTK_WINDOW(window));
193: }
194: 
195: int main(int argc, char** argv) {
196:     // Initialize the static universal DSP context
197:     g_dsp_engine = krisha_dsp_create(48000);
198:     if (g_dsp_engine) {
199:         krisha_preset_t preset;
200:         krisha_dsp_preset_init_flat(&preset);
201:         krisha_dsp_apply_preset(g_dsp_engine, &preset);
202:     }
203: 
204:     precompute_logarithmic_steps();
205: 
206:     GtkApplication* app = gtk_application_new("com.krisha.spoke.linux", G_APPLICATION_DEFAULT_FLAGS);
207:     g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
208:     int status = g_application_run(G_APPLICATION(app), argc, argv);
209:     g_object_unref(app);
210: 
211:     if (g_dsp_engine) {
212:         krisha_dsp_destroy(g_dsp_engine);
213:     }
214:     return status;
215: }
216: 
