#define COLOR_COMPONENT(hex, shift) (((hex >> shift) & 0xFF) / 255.0f)
#define COLOR_ALPHA(hex) COLOR_COMPONENT(hex, 0)
#define COLOR(hex) {COLOR_COMPONENT(hex, 24), \
                    COLOR_COMPONENT(hex, 16), \
                    COLOR_COMPONENT(hex, 8),  \
                    COLOR_ALPHA(hex)}
#define PREMULT_COLOR(hex) {COLOR_COMPONENT(hex, 24) * COLOR_ALPHA(hex), \
                            COLOR_COMPONENT(hex, 16) * COLOR_ALPHA(hex), \
                            COLOR_COMPONENT(hex, 8) * COLOR_ALPHA(hex),  \
                            COLOR_ALPHA(hex)}
#define ANIMATION_DURATION 400.0f // Animation duration in milliseconds
static const float initial_layout_animation_scale = 0.5f; // Set <= 0.0f to keep the initial size
static struct BezierCurve bezier = {
    .control_points = {
        {0.0f, 0.0f},
        {0.05f, 0.9f},
        {0.1f, 1.05f},
        {1.0f, 1.0f}}};
/* gaps */
static int enablegaps = 1; /* enables gaps, used by togglegaps */
static const unsigned int gaps = 10;

static const int sloppyfocus = 1;               /* focus follows mouse */
static const int bypass_surface_visibility = 0; /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx = 2;         /* border pixel of windows */
static const float bordercolor[] = PREMULT_COLOR(0x99999980);
static const float focuscolor[] = PREMULT_COLOR(0xb4befeff);
static const float urgentcolor[] = PREMULT_COLOR(0xb4befeff);
#define TAGCOUNT (9)

static int log_level = WLR_ERROR;

static const char *const autostart[] = {
    "dbus-update-activation-environment", "--systemd", "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP=newl", NULL,
    "waybar", NULL,
    "iwqt", NULL,
    "swaybg", "-i", "/home/agam/.config/wallpaper.jpeg", "-m", "fill", NULL,
    "mako", NULL,
    NULL};

/* NOTE: ALWAYS keep a rule declared even if you don't use rules (e.g leave at least one example) */
static const Rule rules[] = {
    /* app_id             title       tags mask     isfloating   monitor */
    /* examples: */
    {"Gimp_EXAMPLE", NULL, 0, 1, -1},         /* Start on currently visible tags floating, not tiled */
    {"firefox_EXAMPLE", NULL, 1 << 8, 0, -1}, /* Start on ONLY tag "9" */
};

/* layout(s) */
static const Layout layouts[] = {
    /* symbol     arrange function */
    {"[]=", tile},
    {"><>", NULL}, /* no layout function means floating behavior */
    {"[M]", monocle},
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899
 */
/* NOTE: ALWAYS add a fallback rule, even if you are completely sure it won't be used */
static const MonitorRule monrules[] = {
    /* name       mfact  scale layout       rotate/reflect                x    y */
    /* example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
    */
    /* defaults */
    {"eDP-1", 0.55f, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1},
    {NULL, 0.55f, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1},

};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
    /* can specify fields: rules, model, layout, variant, options */
    /* example:
    .options = "ctrl:nocaps",
    */
    .options = NULL,
};
static const int repeat_rate = 50;
static const int repeat_delay = 300;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 0;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_LOGO

#define TAGKEYS(KEY, SKEY, TAG)                                         \
  {MODKEY, KEY, view, {.ui = 1 << TAG}},                                \
      {MODKEY | WLR_MODIFIER_CTRL, KEY, toggleview, {.ui = 1 << TAG}},  \
      {MODKEY | WLR_MODIFIER_SHIFT, SKEY, tag, {.ui = 1 << TAG}},       \
  {                                                                     \
    MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT, SKEY, toggletag, { \
      .ui = 1 << TAG                                                    \
    }                                                                   \
  }
/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd)               \
  {                              \
    .v = (const char *[]) {      \
      "/bin/sh", "-c", cmd, NULL \
    }                            \
  }

/* commands */
static const char *termcmd[] = {"kitty", NULL};
static const char *menucmd[] = {"fuzzel", NULL};

static const Key keys[] = {
    /* Note that Shift changes certain key codes: c -> C, 2 -> at, etc. */
    /* modifier                  key                 function        argument */
    {0, XKB_KEY_XF86MonBrightnessUp, spawn, SHCMD("brightnessctl s +10%")},
    {0, XKB_KEY_XF86MonBrightnessDown, spawn, SHCMD("brightnessctl s 10%-")},
    {0, XKB_KEY_XF86AudioLowerVolume, spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-")},
    {0, XKB_KEY_XF86AudioMute, spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")},
    {0, XKB_KEY_XF86AudioRaiseVolume, spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+")},

    {MODKEY, XKB_KEY_minus, incgaps, {.i = +1}},
    {MODKEY, XKB_KEY_equal, incgaps, {.i = -1}},
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_O, defaultgaps, {0}},
    {MODKEY, XKB_KEY_o, togglegaps, {0}},
    {0, XKB_KEY_Print, spawn, SHCMD("grim -g \"$(slurp -d)\" - | wl-copy && notify-send 'Screenshot taken' 'Copied to clipboard.'")},
    {MODKEY, XKB_KEY_Print, spawn, SHCMD("grim - | wl-copy && notify-send 'Fullscreen screenshot taken' 'Copied to clipboard.'")},
    {MODKEY, XKB_KEY_r, spawn, {.v = menucmd}},
    {MODKEY, XKB_KEY_q, spawn, {.v = termcmd}},
    {MODKEY, XKB_KEY_Right, focusstack, {.i = +1}},
    {MODKEY, XKB_KEY_Left, focusstack, {.i = -1}},
    {MODKEY, XKB_KEY_i, pushlasttop, {0}},
    {MODKEY, XKB_KEY_d, pushfirstbottom, {0}},
    {MODKEY, XKB_KEY_h, setmfact, {.f = -0.05f}},
    {MODKEY, XKB_KEY_l, setmfact, {.f = +0.05f}},
    {MODKEY, XKB_KEY_Return, zoom, {0}},
    {MODKEY, XKB_KEY_b, togglebar, {0}},
    {MODKEY, XKB_KEY_Tab, view, {0}},
    {MODKEY, XKB_KEY_c, killclient, {0}},
    {MODKEY, XKB_KEY_t, setlayout, {.v = &layouts[0]}},
    {MODKEY, XKB_KEY_f, setlayout, {.v = &layouts[1]}},
    {MODKEY, XKB_KEY_m, setlayout, {.v = &layouts[2]}},
    {MODKEY, XKB_KEY_space, setlayout, {0}},
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_space, togglefloating, {0}},
    {MODKEY, XKB_KEY_e, togglefullscreen, {0}},
    {MODKEY, XKB_KEY_0, view, {.ui = ~0}},
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_parenright, tag, {.ui = ~0}},
    {MODKEY, XKB_KEY_comma, focusmon, {.i = WLR_DIRECTION_LEFT}},
    {MODKEY, XKB_KEY_period, focusmon, {.i = WLR_DIRECTION_RIGHT}},
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_less, tagmon, {.i = WLR_DIRECTION_LEFT}},
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_greater, tagmon, {.i = WLR_DIRECTION_RIGHT}},
    TAGKEYS(XKB_KEY_1, XKB_KEY_exclam, 0),
    TAGKEYS(XKB_KEY_2, XKB_KEY_at, 1),
    TAGKEYS(XKB_KEY_3, XKB_KEY_numbersign, 2),
    TAGKEYS(XKB_KEY_4, XKB_KEY_dollar, 3),
    TAGKEYS(XKB_KEY_5, XKB_KEY_percent, 4),
    TAGKEYS(XKB_KEY_6, XKB_KEY_asciicircum, 5),
    TAGKEYS(XKB_KEY_7, XKB_KEY_ampersand, 6),
    TAGKEYS(XKB_KEY_8, XKB_KEY_asterisk, 7),
    TAGKEYS(XKB_KEY_9, XKB_KEY_parenleft, 8),
    {MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit, {0}},

    /* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
    {WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_Terminate_Server, quit, {0}},
/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
 * do not remove them.
 */
#define CHVT(n)                                                              \
  {                                                                          \
    WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, chvt, { \
      .ui = (n)                                                              \
    }                                                                        \
  }
    CHVT(1),
    CHVT(2),
    CHVT(3),
    CHVT(4),
    CHVT(5),
    CHVT(6),
    CHVT(7),
    CHVT(8),
    CHVT(9),
    CHVT(10),
    CHVT(11),
    CHVT(12),
};

static const Button buttons[] = {
    {MODKEY, BTN_LEFT, moveresize, {.ui = CurMove}},
    {MODKEY, BTN_MIDDLE, togglefloating, {0}},
    {MODKEY, BTN_RIGHT, moveresize, {.ui = CurResize}},
};
