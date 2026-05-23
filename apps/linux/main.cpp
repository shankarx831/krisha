/**
 * @file main.cpp
 * @brief Linux GTK4 and Cairo System Tray Spoke wrapping the frozen C++ DSP Core.
 * Supports Phase 2 dynamic multi-curve logarithmic graphing updates.
 */

#include <gtk/gtk.h>
#include <cairo.h>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include "krisha_dsp.h"

static krisha_dsp_engine_t* g_dsp_engine = nullptr;
static krisha_dsp_engine_t* g_target_engine = nullptr;
static GtkWidget* g_graph_drawing_area = nullptr;
static float g_preamp_left = 0.0f;
static float g_preamp_right = 0.0f;
static bool g_bypass = false;

// 120 Logarithmic steps calculation
static const int STEPS_COUNT = 120;
static std::vector<float> g_frequencies(STEPS_COUNT);

static void precompute_logarithmic_steps() {
    double log_min = std::log10(20.0);
    double log_max = std::log10(20000.0);
    double step = (log_max - log_min) / (STEPS_COUNT - 1);

    for (int i = 0; i < STEPS_COUNT; i++) {
        g_frequencies[i] = std::pow(10.0, log_min + i * step);
    }
}

// Analog-style soft-clamping boundaries helper to prevent visual flatlining
static float soft_clamp(float db) {
    const float min_val = -12.0f;
    const float max_val = 12.0f;
    if (db > max_val) {
        return max_val + 2.0f * std::tanh((db - max_val) / 2.0f);
    } else if (db < min_val) {
        return min_val + 2.0f * std::tanh((db - min_val) / 2.0f);
    }
    return db;
}

// Cairo drawing callback for high-performance logarithmic response plotting
static void on_draw_cairo(GtkDrawingArea* drawing_area, cairo_t* cr, int width, int height, gpointer user_data) {
    // Clear background with premium slate gray (#121214)
    cairo_set_source_rgb(cr, 0.07, 0.07, 0.08);
    cairo_paint(cr);

    double left_padding = 45.0;
    double bottom_padding = 20.0;
    double grid_width = width - left_padding;
    double grid_height = height - bottom_padding;

    double log_min = std::log10(20.0);
    double log_max = std::log10(20000.0);

    // 1. Draw horizontal decibel grids and Y-axis labels
    std::vector<float> grid_dbs = {12.0f, 6.0f, 0.0f, -6.0f, -12.0f};
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8.0);

    for (float db : grid_dbs) {
        double y_ratio = 1.0 - (db + 12.0) / 24.0;
        double y = y_ratio * grid_height;
        
        // Draw grid line inside plot bounding box
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_move_to(cr, left_padding, y);
        cairo_line_to(cr, width, y);
        cairo_stroke(cr);

        // Draw Y-axis decibel labels right-aligned inside the left gutter [0, 45]
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%.0fdB", db > 0 ? "+" : "", db);
        
        cairo_text_extents_t extents;
        cairo_text_extents(cr, buf, &extents);
        // Right-aligned to x = 40.0 (leaving a 5px padding from the axis at 45.0)
        cairo_move_to(cr, 40.0 - extents.x_advance, y + extents.height / 2.0);
        cairo_show_text(cr, buf);
    }

    // 2. Draw clean vertical axis line at x = 45.0
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_move_to(cr, left_padding, 0);
    cairo_line_to(cr, left_padding, grid_height);
    cairo_stroke(cr);

    // 3. Draw vertical logarithmic grid lines
    // Major vertical lines (20Hz, 100Hz, 1kHz, 10kHz, 20kHz)
    std::vector<float> major_freqs = {20, 100, 1000, 10000, 20000};
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    for (float f : major_freqs) {
        double x_ratio = (std::log10(f) - log_min) / (log_max - log_min);
        double x = left_padding + x_ratio * grid_width;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, grid_height);
        cairo_stroke(cr);
    }

    // Intermediate vertical log lines (50Hz, 200Hz, 500Hz, 2kHz, 5kHz)
    std::vector<float> inter_freqs = {50, 200, 500, 2000, 5000};
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    double inter_dashes[] = {4.0, 4.0};
    cairo_set_dash(cr, inter_dashes, 2, 0.0);
    for (float f : inter_freqs) {
        double x_ratio = (std::log10(f) - log_min) / (log_max - log_min);
        double x = left_padding + x_ratio * grid_width;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, grid_height);
        cairo_stroke(cr);
    }
    cairo_set_dash(cr, nullptr, 0, 0.0); // Reset dash

    // 4. Draw outer border of active grid area
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_rectangle(cr, left_padding, 0, grid_width, grid_height);
    cairo_stroke(cr);

    // 5. Draw X-axis frequency labels in the bottom gutter centered under their lines
    struct FreqLabel {
        float freq;
        const char* text;
    };
    std::vector<FreqLabel> major_labels = {
        {20.0f, "20Hz"},
        {100.0f, "100Hz"},
        {1000.0f, "1kHz"},
        {10000.0f, "10kHz"},
        {20000.0f, "20kHz"}
    };
    for (const auto& label : major_labels) {
        double x_ratio = (std::log10(label.freq) - log_min) / (log_max - log_min);
        double x = left_padding + x_ratio * grid_width;
        
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label.text, &extents);
        cairo_move_to(cr, x - extents.width / 2.0, height - 10.0 + extents.height / 2.0);
        cairo_show_text(cr, label.text);
    }

    if (!g_dsp_engine || !g_target_engine) return;

    // Precompute the 4 synchronize magnitude curves
    std::vector<float> harman_curve(STEPS_COUNT);
    std::vector<float> raw_curve(STEPS_COUNT);
    std::vector<float> eq_curve(STEPS_COUNT);
    std::vector<float> final_curve(STEPS_COUNT);

    for (int i = 0; i < STEPS_COUNT; i++) {
        float freq = g_frequencies[i];
        float harman_db = krisha_dsp_get_harman_target_at_frequency(freq);
        float eq_db = g_bypass ? g_preamp_left : krisha_dsp_get_magnitude_at_frequency(g_dsp_engine, freq, true);
        float target_eq_db = krisha_dsp_get_magnitude_at_frequency(g_target_engine, freq, true);
        float raw_db = harman_db - target_eq_db;
        float final_db = raw_db + eq_db;

        harman_curve[i] = soft_clamp(harman_db);
        raw_curve[i] = soft_clamp(raw_db);
        eq_curve[i] = soft_clamp(eq_db);
        final_curve[i] = soft_clamp(final_db);
    }

    // 6. Set clipping mask for the curves so they stay strictly within the inner dimensions
    cairo_save(cr);
    cairo_rectangle(cr, left_padding, 0, grid_width, grid_height);
    cairo_clip(cr);

    // --- LAYER 1: Line 3 (Raw Response Curve - Ultra-thin, 20% opacity white)
    cairo_set_line_width(cr, 0.75);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.20);
    for (int i = 0; i < STEPS_COUNT; i++) {
        double x_ratio = (double)i / (STEPS_COUNT - 1);
        double x = left_padding + x_ratio * grid_width;
        double y_ratio = 1.0 - (raw_curve[i] + 12.0) / 24.0;
        double y = y_ratio * grid_height;

        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // --- LAYER 2: Line 4 (Equalizer Filter Curve - Ultra-thin, 20% opacity white)
    cairo_set_line_width(cr, 0.75);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.20);
    for (int i = 0; i < STEPS_COUNT; i++) {
        double x_ratio = (double)i / (STEPS_COUNT - 1);
        double x = left_padding + x_ratio * grid_width;
        double y_ratio = 1.0 - (eq_curve[i] + 12.0) / 24.0;
        double y = y_ratio * grid_height;

        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // --- LAYER 3: Line 2 (Target Curve - Harman Baseline - Thin solid white at 60% alpha)
    cairo_set_line_width(cr, 1.25);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.60);
    for (int i = 0; i < STEPS_COUNT; i++) {
        double x_ratio = (double)i / (STEPS_COUNT - 1);
        double x = left_padding + x_ratio * grid_width;
        double y_ratio = 1.0 - (harman_curve[i] + 12.0) / 24.0;
        double y = y_ratio * grid_height;

        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // --- LAYER 4: Line 1 (Final Equalized Result - Solid primary system accent blue at 100% alpha)
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 0.0, 0.48, 1.0); // #007AFF at 100% alpha
    for (int i = 0; i < STEPS_COUNT; i++) {
        double x_ratio = (double)i / (STEPS_COUNT - 1);
        double x = left_padding + x_ratio * grid_width;
        double y_ratio = 1.0 - (final_curve[i] + 12.0) / 24.0;
        double y = y_ratio * grid_height;

        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // 7. Draw Minimalist Instrument-Grade Legend HUD in the top-right corner
    double legend_y = 12.0;
    double legend_x_start = width - 210.0;
    
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8.0);

    // Legend Item 1: Equalized
    cairo_set_source_rgb(cr, 0.0, 0.48, 1.0); // Blue
    cairo_arc(cr, legend_x_start, legend_y - 2.5, 3.0, 0, 2 * M_PI);
    cairo_fill(cr);
    
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
    cairo_move_to(cr, legend_x_start + 8.0, legend_y);
    cairo_show_text(cr, "Equalized");
    
    // Legend Item 2: Target
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.60); // White 60%
    cairo_arc(cr, legend_x_start + 70.0, legend_y - 2.5, 3.0, 0, 2 * M_PI);
    cairo_fill(cr);
    
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
    cairo_move_to(cr, legend_x_start + 78.0, legend_y);
    cairo_show_text(cr, "Target");
    
    // Legend Item 3: Raw / Filter
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.20); // Translucent White 20%
    cairo_arc(cr, legend_x_start + 130.0, legend_y - 2.5, 3.0, 0, 2 * M_PI);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
    cairo_move_to(cr, legend_x_start + 138.0, legend_y);
    cairo_show_text(cr, "Raw / Filter");

    // Restore Cairo context to release clipping mask
    cairo_restore(cr);
}

// Slider update callbacks
static void on_preamp_left_changed(GtkRange* range, gpointer user_data) {
    g_preamp_left = gtk_range_get_value(range);
    if (g_dsp_engine) {
        krisha_dsp_update_preamp_left(g_dsp_engine, g_preamp_left);
    }
    gtk_widget_queue_draw(g_graph_drawing_area);
}

static void on_preamp_right_changed(GtkRange* range, gpointer user_data) {
    g_preamp_right = gtk_range_get_value(range);
    if (g_dsp_engine) {
        krisha_dsp_update_preamp_right(g_dsp_engine, g_preamp_right);
    }
    gtk_widget_queue_draw(g_graph_drawing_area);
}

static void on_bypass_toggled(GtkToggleButton* button, gpointer user_data) {
    g_bypass = gtk_toggle_button_get_active(button);
    if (g_dsp_engine) {
        krisha_dsp_set_bypass(g_dsp_engine, g_bypass);
    }
    gtk_widget_queue_draw(g_graph_drawing_area);
}

static void activate(GtkApplication* app, gpointer user_data) {
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "KRISHA Universal - Linux Spoke UI");
    gtk_window_set_default_size(GTK_WINDOW(window), 550, 420);

    // Modern dark-themed container layout
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_all(main_box, 20);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    // Sleek title HUD
    GtkWidget* title_label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title_label), "<span weight='bold' size='x-large' foreground='#007AFF'>KRISHA</span> <span size='x-large' foreground='#FFFFFF'>Universal</span>");
    gtk_box_append(GTK_BOX(main_box), title_label);

    // Cairo Live Graph
    g_graph_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_graph_drawing_area, -1, 180);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_graph_drawing_area), on_draw_cairo, nullptr, nullptr);
    gtk_box_append(GTK_BOX(main_box), g_graph_drawing_area);

    // Preamp Balancer panel (Zero polling reactive sliders)
    GtkWidget* sliders_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
    gtk_box_append(GTK_BOX(main_box), sliders_box);

    // Left Preamp Offset
    GtkWidget* left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(left_box, TRUE);
    GtkWidget* left_lbl = gtk_label_new("Preamp Left (dB)");
    gtk_widget_set_halign(left_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_box), left_lbl);
    GtkWidget* left_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 12.0, 0.1);
    gtk_range_set_value(GTK_RANGE(left_slider), 0.0);
    g_signal_connect(left_slider, "value-changed", G_CALLBACK(on_preamp_left_changed), nullptr);
    gtk_box_append(GTK_BOX(left_box), left_slider);
    gtk_box_append(GTK_BOX(sliders_box), left_box);

    // Right Preamp Offset
    GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(right_box, TRUE);
    GtkWidget* right_lbl = gtk_label_new("Preamp Right (dB)");
    gtk_widget_set_halign(right_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right_box), right_lbl);
    GtkWidget* right_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 12.0, 0.1);
    gtk_range_set_value(GTK_RANGE(right_slider), 0.0);
    g_signal_connect(right_slider, "value-changed", G_CALLBACK(on_preamp_right_changed), nullptr);
    gtk_box_append(GTK_BOX(right_box), right_slider);
    gtk_box_append(GTK_BOX(sliders_box), right_box);

    // Bypass controls
    GtkWidget* bypass_btn = gtk_check_button_new_with_label("Bypass DSP Engine");
    g_signal_connect(bypass_btn, "toggled", G_CALLBACK(on_bypass_toggled), nullptr);
    gtk_box_append(GTK_BOX(main_box), bypass_btn);

    // One-Touch Install & Complete System Purge Engine Bar
    GtkWidget* management_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_bottom(management_bar, 5);
    gtk_box_append(GTK_BOX(main_box), management_bar);

    GtkWidget* install_btn = gtk_button_new_with_label("Install Driver / Sync Setup");
    gtk_widget_add_css_class(install_btn, "accent");
    gtk_widget_set_hexpand(install_btn, TRUE);
    g_signal_connect(install_btn, "clicked", G_CALLBACK([](GtkButton* btn, gpointer d) {
        std::thread([]() {
            system("pactl load-module module-null-sink sink_name=krisha_sink sink_properties=device.description=Krisha_Virtual_Sink > /dev/null 2>&1");
        }).detach();
    }), nullptr);
    gtk_box_append(GTK_BOX(management_bar), install_btn);

    GtkWidget* purge_btn = gtk_button_new_with_label("Purge System Files");
    gtk_widget_add_css_class(purge_btn, "destructive");
    gtk_widget_set_hexpand(purge_btn, TRUE);
    g_signal_connect(purge_btn, "clicked", G_CALLBACK([](GtkButton* btn, gpointer d) {
        std::thread([]() {
            system("systemctl --user stop krisha.service > /dev/null 2>&1");
            system("systemctl --user disable krisha.service > /dev/null 2>&1");
            system("rm -rf ~/.config/krisha");
            system("rm -f ~/.config/systemd/user/krisha.service");
            system("rm -f /usr/share/applications/krisha.desktop");
            exit(0);
        }).detach();
    }), nullptr);
    gtk_box_append(GTK_BOX(management_bar), purge_btn);

    // Quit Row
    GtkWidget* quit_btn = gtk_button_new_with_label("Quit KRISHA");
    gtk_widget_add_css_class(quit_btn, "quit");
    g_signal_connect(quit_btn, "clicked", G_CALLBACK([](GtkButton* btn, gpointer d) {
        exit(0);
    }), nullptr);
    gtk_box_append(GTK_BOX(main_box), quit_btn);

    // Custom theme styling with CSS Provider
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window { background-color: #121212; }"
        "label { color: #FFFFFF; font-family: sans-serif; font-size: 10pt; }"
        "scale trough { background-color: #2C2C2E; min-height: 4px; border-radius: 2px; }"
        "scale highlight { background-color: #007AFF; border-radius: 2px; }"
        "scale slider { background-color: #E5E5EA; border-radius: 50%; min-width: 14px; min-height: 14px; margin: -5px 0; }"
        "checkbutton { color: #FFFFFF; font-family: sans-serif; font-size: 10pt; }"
        "button.destructive { background-color: #FF3B30; color: #FFFFFF; border-radius: 6px; border: none; padding: 10px; font-weight: bold; font-family: sans-serif; }"
        "button.accent { background-color: #007AFF; color: #FFFFFF; border-radius: 6px; border: none; padding: 10px; font-weight: bold; font-family: sans-serif; }"
        "button.quit { background-color: #2C2C2E; color: #FFFFFF; border-radius: 6px; border: none; padding: 8px; font-family: sans-serif; }", -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Wakeup logic for GMainContext to keep main execution entirely suspended when idle
    GMainContext* ctx = g_main_context_default();
    g_main_context_wakeup(ctx);

    gtk_window_present(GTK_WINDOW(window));
}

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <iostream>

static std::string get_linux_presets_directory() {
    const char* home = getenv("HOME");
    if (!home) return "";
    std::string presets_dir = std::string(home) + "/.config/krisha/presets";
    
    // Create directories if they don't exist
    std::string config_dir = std::string(home) + "/.config";
    mkdir(config_dir.c_str(), 0755);
    std::string krisha_dir = std::string(home) + "/.config/krisha";
    mkdir(krisha_dir.c_str(), 0755);
    mkdir(presets_dir.c_str(), 0755);
    
    return presets_dir;
}

static void save_linux_custom_preset(const std::string& name, const std::string& json_content) {
    std::string dir = get_linux_presets_directory();
    if (dir.empty()) return;
    std::string file_path = dir + "/" + name + ".json";
    std::ofstream out(file_path);
    if (out.is_open()) {
        out << json_content;
        out.close();
        std::cout << "[KRISHA LinuxSpoke] Saved custom preset to: " << file_path << std::endl;
    }
}

int main(int argc, char** argv) {
    // Initialize the static universal DSP context
    g_dsp_engine = krisha_dsp_create(48000);
    g_target_engine = krisha_dsp_create(48000);
    if (g_dsp_engine) {
        krisha_preset_t preset;
        krisha_dsp_preset_init_flat(&preset);
        krisha_dsp_apply_preset(g_dsp_engine, &preset);
        if (g_target_engine) {
            krisha_dsp_apply_preset(g_target_engine, &preset);
        }
    }

    precompute_logarithmic_steps();

    GtkApplication* app = gtk_application_new("com.krisha.spoke.linux", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (g_dsp_engine) {
        krisha_dsp_destroy(g_dsp_engine);
    }
    if (g_target_engine) {
        krisha_dsp_destroy(g_target_engine);
    }
    return status;
}
