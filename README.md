# newl

newl is a suckless wayland compositor based on DWL that doesn't suck in its eyecandy department. 


## Features

- **Extended DWL Functionality:** newl builds on DWL and/or DWM’s familiar simplicity, with customizable animations for window movements and workspace transitions.
- **IPC Support:** Support for IPC through DWL's IPC protocol.
- **Minimalist & Efficient:** With a codebase that embraces the suckless philosophy, newl remains lean and easy to maintain.
- **Configurable Animations:** Adjust the duration and easing of animations using Bezier curves, making your transitions smooth and tailored to your preferences.
- **Customizable Gaps & Borders:** Easily modify gaps between windows and set distinct colors for borders, focused, and urgent windows.
- **Comprehensive Input Support:** Fine-tune keyboard and trackpad settings for an optimized and responsive interaction experience.
- **Extensible Key Bindings:** A highly configurable key-binding system lets you effortlessly manage window focus, layouts, and workspace assignments.


## Installation

Clone the repository and build newl:

```sh
git clone https://github.com/voldtman/newl.git
cd newl
sudo make clean install
```

Be sure to review the [Installation Guide](https://github.com/voldtman/newl#installation) on our GitHub page for detailed instructions and dependencies.


## Configuration Guide

newl’s configuration is written in C and is designed for maximum simplicity and extensibility. Below is an overview of the key configuration sections and how they work.

### 1. Color & Animation Settings

- **Color Macro:**
  ```c
  #define COLOR(hex) { ((hex >> 24) & 0xFF) / 255.0f, \
                         ((hex >> 16) & 0xFF) / 255.0f, \
                         ((hex >> 8) & 0xFF) / 255.0f, \
                         (hex & 0xFF) / 255.0f }
  ```
  This macro converts a 32-bit hexadecimal value into normalized RGBA components. For example, `COLOR(0x999999ff)` sets a gray color with full opacity.

- **Animation Settings:**
  ```c
  #define ANIMATION_DURATION 600.0f  // Animation duration in milliseconds

  static struct BezierCurve bezier = {
      .control_points = {
          {0.0f, 0.0f},
          {0.05f, 0.9f},
          {0.1f, 1.05f},
          {1.0f, 1.0f}
      }
  };
  ```
  These settings define the duration and easing function for window and workspace animations. Adjust the Bezier curve control points to fine-tune the animation's acceleration and deceleration.

### 2. Window Gaps, Focus, and Borders

- **Gaps & Focus:**
  ```c
  static int enablegaps = 1;   /* enables gaps, used by togglegaps */
  static const unsigned int gaps = 10;
  static const int sloppyfocus = 1;  /* focus follows mouse */
  ```
  Enable gaps between windows and set the gap size, as well as enable “sloppy focus” where focus follows the mouse pointer.

- **Borders:**
  ```c
  static const unsigned int borderpx = 2;  /* border pixel of windows */
  static const float bordercolor[] = COLOR(0x999999ff);
  static const float focuscolor[] = COLOR(0xb4befeff);
  static const float urgentcolor[] = COLOR(0xb4befeff);
  ```
  Configure the window border width and color for normal, focused, and urgent states.

### 3. Tag & Rule Definitions

- **Tags:**
  ```c
  #define TAGCOUNT (9)
  ```
  Define nine virtual workspaces (tags).

- **Rules:**
  ```c
  static const Rule rules[] = {
      /* app_id, title, tags mask, isfloating, monitor */
      { "Gimp_EXAMPLE",     NULL,       0,            1,           -1 },
      { "firefox_EXAMPLE",  NULL,       1 << 8,       0,           -1 },
  };
  ```
  Map applications to specific workspaces, floating behavior, or monitor assignment.

### 4. Layouts & Monitor Configuration

- **Layouts:**
  ```c
  static const Layout layouts[] = {
      { "[]=",      tile },
      { "><>",      NULL },    /* floating layout */
      { "[M]",      monocle },
  };
  ```
  Choose between tiled, floating, and monocle (full-screen) layouts.

- **Monitor Rules:**
  ```c
  static const MonitorRule monrules[] = {
      { "eDP-1", 0.55f, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1 },
  };
  ```
  Set default monitor parameters, including layout and scaling factors.

### 5. Input Device Settings

- **Keyboard Configuration:**
  ```c
  static const struct xkb_rule_names xkb_rules = {
      .options = NULL,
  };
  static const int repeat_rate = 50;
  static const int repeat_delay = 300;
  ```
  Configure keyboard behavior such as key repeat rate and delay.

- **Trackpad Settings:**
  ```c
  static const int tap_to_click = 1;
  static const int tap_and_drag = 1;
  static const int drag_lock = 0;
  static const int natural_scrolling = 0;
  static const int disable_while_typing = 1;
  static const int left_handed = 0;
  static const int middle_button_emulation = 0;
  static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
  static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
  static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
  static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
  static const double accel_speed = 0.0;
  static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
  ```
  Customize trackpad behavior, including tap-to-click, scrolling, and acceleration.

### 6. Key & Mouse Bindings

- **Modifier and Helper Macros:**
  ```c
  #define MODKEY WLR_MODIFIER_LOGO
  #define TAGKEYS(KEY,SKEY,TAG) \
      { MODKEY, KEY, view, {.ui = 1 << TAG} }, \
      { MODKEY|WLR_MODIFIER_CTRL, KEY, toggleview, {.ui = 1 << TAG} }, \
      { MODKEY|WLR_MODIFIER_SHIFT, SKEY, tag, {.ui = 1 << TAG} }, \
      { MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, SKEY, toggletag, {.ui = 1 << TAG} }
  #define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }
  ```
  These simplify the process of mapping key combinations to functions and shell commands.

- **Autostart Applications:**
  ```c
  static const char *const autostart[] = {
      "dbus-update-activation-environment", "--systemd", "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP=dwl", NULL,
      "systemctl", "--user", "start", "hyprpolkitagent", NULL,
      "waybar", NULL,
      "nm-applet", NULL,
      "swaybg", "-i", "/home/agam/.config/wallpaper.jpg", "-m", "fill", NULL,
      "mako", NULL,
      NULL
  };
  ```
  These commands run automatically when newl starts.

- **Key Bindings:**
  The configuration includes key bindings for brightness, audio, gap adjustments, window management, and more. For example:
  ```c
  { MODKEY, XKB_KEY_minus, incgaps, {.i = +1 } },
  { MODKEY, XKB_KEY_equal, incgaps, {.i = -1 } },
  { MODKEY, XKB_KEY_r, spawn, {.v = menucmd} },
  { MODKEY, XKB_KEY_q, spawn, {.v = termcmd} },
  { MODKEY, XKB_KEY_Return, zoom, {0} },
  { MODKEY, XKB_KEY_c, killclient, {0} },
  // ... additional key bindings ...
  TAGKEYS( XKB_KEY_1, XKB_KEY_exclam, 0),
  // ... tag keys for 2-9 ...
  { MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit, {0} },
  ```
  Refer to the source code for the complete list.

- **Mouse Bindings:**
  ```c
  static const Button buttons[] = {
      { MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
      { MODKEY, BTN_MIDDLE, togglefloating, {0} },
      { MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
  };
  ```
  These settings control window movement and resizing with the mouse when combined with the modifier key.


## Contributing

Contributions to newl are welcome! Please refer to our [CONTRIBUTING.md](https://github.com/voldtman/newl/blob/main/CONTRIBUTING.md) for guidelines on submitting patches, reporting issues, or suggesting improvements.


## Acknowledgements

newl is inspired by and extends the functionalities of [dwl](https://codeberg.org/dwl/dwl). We gratefully acknowledge the DWL team for laying the foundation of a minimalist, extendable compositor, which served as the starting point for newl.


## License

newl is released under the GPL-3 License. See the [LICENSE](https://github.com/voldtman/newl/blob/main/LICENSE) file for details.


## Additional Resources

- [Project Repository on GitHub](https://github.com/voldtman/newl)
- [Issue Tracker](https://github.com/voldtman/newl/issues)
