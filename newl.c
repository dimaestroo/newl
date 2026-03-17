#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif
#include "dwl-ipc-unstable-v2-protocol.h"

static void die(const char *fmt, ...);
static void *ecalloc(size_t nmemb, size_t size);
static int fd_set_nonblock(int fd);
static void terminatechild(pid_t pid, int kill_group);

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEONTAGS(C, M, T) ((M) && (C)->mon == (M) && ((C)->tags & (T)))
#define VISIBLEON(C, M) VISIBLEONTAGS((C), (M), (M)->tagset[(M)->seltags])
#define LENGTH(X) (sizeof X / sizeof X[0])
#define END(A) ((A) + LENGTH(A))
#define TAGMASK ((1u << TAGCOUNT) - 1)
#define CONFIGURE_TIMEOUT_MS 100
#define LISTEN(E, L, H) wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)                           \
  do {                                                \
    struct wl_listener *_l = ecalloc(1, sizeof(*_l)); \
    _l->notify = (H);                                 \
    wl_signal_add((E), _l);                           \
  } while (0)
#define BAKED_POINTS 255
#define INV_BAKED_POINTS (1.0f / BAKED_POINTS)
enum {
  CurNormal,
  CurPressed,
  CurMove,
  CurResize
};
enum {
  XDGShell,
  LayerShell,
  X11
};
enum {
  LyrBg,
  LyrBottom,
  LyrTile,
  LyrFloat,
  LyrTop,
  LyrFS,
  LyrOverlay,
  LyrBlock,
  NUM_LAYERS
};
typedef union {
  int i;
  uint32_t ui;
  float f;
  const void *v;
} Arg;

typedef struct
{
  unsigned int mod;
  unsigned int button;
  void (*func)(const Arg *);
  const Arg arg;
} Button;
typedef struct Monitor Monitor;
typedef struct
{
  struct wlr_box start;
  struct wlr_box target;
  struct wlr_box projected;
  struct timespec start_time;
  float start_opacity;
  float target_opacity;
  bool active;
} Animation;
typedef struct
{
  unsigned int type;
  Monitor *mon;
  struct wlr_scene_tree *scene;
  struct wlr_scene_rect *border[4];
  struct wlr_scene_tree *scene_surface;
  struct wl_list link;
  struct wlr_box geom;
  struct wlr_box current_geom;
  struct wlr_box prev;
  struct wlr_box bounds;
  union {
    struct wlr_xdg_surface *xdg;
    struct wlr_xwayland_surface *xwayland;
  } surface;
  struct wlr_xdg_toplevel_decoration_v1 *decoration;
  struct wl_listener commit;
  struct wl_listener map;
  struct wl_listener maximize;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener set_title;
  struct wl_listener fullscreen;
  struct wl_listener set_decoration_mode;
  struct wl_listener destroy_decoration;
#ifdef XWAYLAND
  struct wl_listener activate;
  struct wl_listener associate;
  struct wl_listener dissociate;
  struct wl_listener configure;
  struct wl_listener set_hints;
#endif
  unsigned int bw;
  uint32_t tags;
  uint32_t pending_configure_serial;
  int isfloating, isurgent, isfullscreen;
  Animation anim;
  float opacity;
  struct wlr_box restore_geom;
  bool is_tag_switch_anim;
  bool hide_on_anim_end;
  bool close_pending;
  unsigned int initial_position;
  struct wl_event_source *pending_configure_timeout;
} Client;
typedef struct
{
  struct wl_list link;
  struct wlr_scene_buffer *scene_buffer;
  float opacity;
} CloseOverlayBuffer;
typedef struct
{
  struct wl_list link;
  struct wlr_scene_rect *scene_rect;
  float color[4];
} CloseOverlayRect;
typedef struct
{
  struct wl_list link;
  Monitor *mon;
  struct wlr_scene_tree *scene;
  struct wl_list buffers;
  struct wl_list rects;
  struct wlr_box geom;
  struct timespec start_time;
} CloseOverlay;
typedef struct
{
  CloseOverlay *overlay;
  Client *client;
} CloseOverlaySnapshotData;
typedef struct
{
  struct wl_list link;
  struct wl_resource *resource;
  Monitor *mon;
} DwlIpcOutput;
typedef struct
{
  uint32_t mod;
  xkb_keysym_t keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;
typedef struct
{
  struct wlr_keyboard_group *wlr_group;
  int nsyms;
  const xkb_keysym_t *keysyms;
  uint32_t mods;
  struct wl_event_source *key_repeat_source;
  struct wl_listener modifiers;
  struct wl_listener key;
  struct wl_listener destroy;
} KeyboardGroup;
typedef struct
{
  unsigned int type;
  Monitor *mon;
  struct wlr_scene_tree *scene;
  struct wlr_scene_tree *popups;
  struct wlr_scene_layer_surface_v1 *scene_layer;
  struct wl_list link;
  int mapped;
  Animation anim;
  struct wlr_box geom;
  float opacity;
  bool being_unmapped;
  unsigned int initial_position;
  struct wlr_layer_surface_v1 *layer_surface;
  struct wl_listener destroy;
  struct wl_listener unmap;
  struct wl_listener surface_commit;
} LayerSurface;
typedef struct
{
  const char *symbol;
  void (*arrange)(Monitor *);
} Layout;
struct Monitor {
  struct wl_list link;
  struct wl_list dwl_ipc_outputs;
  struct wlr_output *wlr_output;
  struct wlr_scene_output *scene_output;
  struct wl_listener frame;
  struct wl_listener destroy;
  struct wl_listener request_state;
  struct wl_listener destroy_lock_surface;
  struct wlr_session_lock_surface_v1 *lock_surface;
  struct wlr_box m;
  struct wlr_box w;
  struct wl_list layers[4];
  const Layout *lt[2];
  unsigned int seltags;
  unsigned int sellt;
  uint32_t tagset[2];
  uint32_t prevtagset;
  int switch_offset;
  struct timespec switch_start_time;
  float mfact;
  int gamma_lut_changed;
  char ltsymbol[16];
  int asleep;
  int switch_animate;
  int switch_dir;
  int gaps;
};
typedef struct
{
  const char *name;
  float mfact;
  float scale;
  const Layout *lt;
  enum wl_output_transform rr;
  int x, y;
} MonitorRule;
typedef struct
{
  struct wlr_pointer_constraint_v1 *constraint;
  struct wl_listener destroy;
} PointerConstraint;
typedef struct
{
  const char *id;
  const char *title;
  uint32_t tags;
  int isfloating;
  int monitor;
} Rule;
typedef struct
{
  struct wlr_scene_tree *scene;

  struct wlr_session_lock_v1 *lock;
  struct wl_listener new_surface;
  struct wl_listener unlock;
  struct wl_listener destroy;
} SessionLock;
struct Vec2 {
  float x;
  float y;
};
struct BezierCurve {
  struct Vec2 control_points[4];
  struct Vec2 baked_points[BAKED_POINTS];
};

static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
                         struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void autostartexec(void);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
                         double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
                         double sx, double sy, uint32_t time);
static void printstatus(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, uint32_t newtags);
static void setmonitortags(Monitor *m, uint32_t newtags, int toggle_tagset);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
                     Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);
static void swipe_begin(struct wl_listener *listener, void *data);
static void swipe_update(struct wl_listener *listener, void *data);
static void swipe_end(struct wl_listener *listener, void *data);
static void pinch_begin(struct wl_listener *listener, void *data);
static void pinch_update(struct wl_listener *listener, void *data);
static void pinch_end(struct wl_listener *listener, void *data);
static void hold_begin(struct wl_listener *listener, void *data);
static void hold_end(struct wl_listener *listener, void *data);
static void togglebar(const Arg *arg);
static void dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void dwl_ipc_manager_destroy(struct wl_resource *resource);
static void dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output);
static void dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource);
static void dwl_ipc_output_destroy(struct wl_resource *resource);
static void dwl_ipc_output_printstatus(Monitor *monitor);
static void dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output);
static void dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags);
static void dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index);
static void dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset);
static void dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource);
static void init_bezier(void);
static float get_x_for_t(const struct BezierCurve *curve, float t);
static float get_y_for_t(const struct BezierCurve *curve, float t);
static float get_y_for_point(const struct BezierCurve *curve, float x);
static void sample_animation_timing(const struct timespec *start_time,
                                    const struct timespec *now,
                                    float *progress, float *eased_progress);
static void sample_animation(const Animation *anim, float t, struct wlr_box *geom,
                             float *opacity);
static void sample_animation_state(const Animation *anim, const struct timespec *now,
                                   struct wlr_box *geom, float *opacity,
                                   float *progress, float *eased_progress);
static int client_is_switch_exiting(Client *c);
static void get_client_settled_geometry(Client *c, struct wlr_box *geom);
static int get_monitor_switch_offset(Monitor *m, uint32_t tags);
static void get_layer_surface_geom(LayerSurface *l, struct wlr_box *geom);
static void set_layer_surface_geom(LayerSurface *l, const struct wlr_box *geom);
static void apply_client_opacity(Client *c, float opacity);
static void apply_layer_surface_opacity(LayerSurface *l, float opacity);
static const float *client_border_base_color(Client *c);
static void update_client_border_color(Client *c);
static void get_projected_animation_box(const Animation *anim, struct wlr_box *projected);
static void client_apply_visual_geometry(Client *c, const struct wlr_box *geo);
static void client_apply_scene_geometry(Client *c, const struct wlr_box *geo);
static int client_is_interactive_resize(Client *c);
static int client_should_use_visual_geometry(Client *c,
                                             const struct wlr_box *current,
                                             const struct wlr_box *visual);
static void client_apply_current_visual_geometry(Client *c,
                                                 const struct wlr_box *current,
                                                 const struct wlr_box *visual,
                                                 int immediate_scene);
static void client_apply_sampled_opacity(Client *c);
static void client_init_common(Client *c, unsigned int type, unsigned int bw,
                               unsigned int initial_position);
static void client_apply_committed_geometry(Client *c, int width, int height);
static void client_get_requested_surface_size(Client *c, const struct wlr_box *geo,
                                              const struct wlr_box *prev,
                                              uint32_t *width, uint32_t *height);
static void client_clear_pending_configure(Client *c);
static int client_handle_configure_timeout(void *data);
static int client_get_xdg_committed_size(Client *c, struct wlr_box *size);
static void client_handle_x11_commit(Client *c);
static void client_request_geometry(Client *c, const struct wlr_box *geo, int interact);
static void client_request_surface_size(Client *c, const struct wlr_box *geo,
                                        const struct wlr_box *prev);
static void client_set_scheduled_size(Client *c, const struct wlr_box *geo);
static void client_set_pending_configure_serial(Client *c, uint32_t serial);
static void client_create_scene(Client *c);
static void client_create_borders(Client *c);
static void client_bootstrap_geometry(Client *c);
static int client_map_unmanaged(Client *c);
static void client_finish_managed_map(Client *c, Client *parent);
static void client_apply_x11_configure_request(
    Client *c, struct wlr_xwayland_surface_configure_event *event,
    const struct wlr_box *geo);
static void start_animation_at(Client *c, const struct wlr_box *target,
                               const struct wlr_box *start, float target_opacity,
                               const struct timespec *start_time);
static int client_has_initial_position(Client *c, Monitor *m);
static int prepare_initial_client_position(Client *c, Monitor *m,
                                           const struct wlr_box *target,
                                           struct wlr_box *start);
static void apply_initial_box_offset(struct wlr_box *box, int dir_x, int dir_y);
static void start_layout_animation(Client *c, Monitor *m, const struct wlr_box *target);
static void start_tag_switch_exit_animation(Client *c, Monitor *m);
static void prepare_layer_surface_initial_position(LayerSurface *l,
                                                   const struct wlr_box *target,
                                                   struct wlr_box *start);
static void start_layer_surface_animation(LayerSurface *l, const struct wlr_box *target,
                                          const struct wlr_box *start,
                                          float start_opacity, float target_opacity);
static void retarget_layer_surface(LayerSurface *l, const struct wlr_box *target);
static void snapshot_close_overlay_buffer(struct wlr_scene_buffer *buffer, int sx, int sy,
                                          void *data);
static void snapshot_close_overlay_tree(CloseOverlay *overlay,
                                        struct wlr_scene_tree *scene_tree, Client *client);
static void transfer_client_borders_to_close_overlay(CloseOverlay *overlay,
                                                     Client *client);
static CloseOverlay *create_close_overlay_base(Monitor *mon,
                                               const struct wlr_box *geom);
static void activate_close_overlay(CloseOverlay *overlay);
static void create_close_overlay(Client *c, const struct wlr_box *geom);
static void create_layer_surface_close_overlay(LayerSurface *l, const struct wlr_box *geom);
static void destroy_close_overlay(CloseOverlay *overlay);
static void cancel_tag_switch_exit_animations(Monitor *m);
static void apply_close_overlay_opacity(CloseOverlay *overlay, float opacity);
static int step_client_animation_frame(Client *c, const struct timespec *now);
static int step_layer_surface_animation_frame(LayerSurface *l,
                                              const struct timespec *now);
static int step_close_overlay_frame(CloseOverlay *overlay,
                                    const struct timespec *now);
static void start_close_animation(Client *c);
static void pushlasttop(const Arg *arg);
static void pushfirstbottom(const Arg *arg);
static void setgaps(int o);
static void togglegaps(const Arg *arg);
static void defaultgaps(const Arg *arg);
static void incgaps(const Arg *arg);

static pid_t child_pid = -1;
static pid_t *autostart_pids;
static size_t autostart_len;
static int locked;
static void *exclusive_focus;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *close_overlay_tree;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;

static const int layermap[] = {LyrBg, LyrBottom, LyrTop, LyrOverlay};
static float bezier_max_y = 1.0f;
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients;
static struct wl_list close_overlays;
static Client *focus_anchor;
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;
static struct wlr_pointer_gestures_v1 *pointer_gestures;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy;

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;

static struct wl_listener swipebegin = {.notify = swipe_begin};
static struct wl_listener swipeupdate = {.notify = swipe_update};
static struct wl_listener swipeend = {.notify = swipe_end};
static struct wl_listener pinchbegin = {.notify = pinch_begin};
static struct wl_listener pinchupdate = {.notify = pinch_update};
static struct wl_listener pinchend = {.notify = pinch_end};
static struct wl_listener holdbegin = {.notify = hold_begin};
static struct wl_listener holdend = {.notify = hold_end};

static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};

static struct zdwl_ipc_manager_v2_interface dwl_manager_implementation = {.release = dwl_ipc_manager_release, .get_output = dwl_ipc_manager_get_output};
static struct zdwl_ipc_output_v2_interface dwl_output_implementation = {.release = dwl_ipc_output_release, .set_tags = dwl_ipc_output_set_tags, .set_layout = dwl_ipc_output_set_layout, .set_client_tags = dwl_ipc_output_set_client_tags};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
static void xwaylandready(struct wl_listener *listener, void *data);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
#endif

#include "config.h"

static void
die(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    fputc(' ', stderr);
    perror(NULL);
  } else {
    fputc('\n', stderr);
  }

  exit(1);
}

static void *
ecalloc(size_t nmemb, size_t size) {
  void *p;

  if (!(p = calloc(nmemb, size)))
    die("calloc:");
  return p;
}

static int
fd_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL);

  if (flags < 0) {
    perror("fcntl(F_GETFL):");
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("fcntl(F_SETFL):");
    return -1;
  }
  return 0;
}

static void
terminatechild(pid_t pid, int kill_group) {
  static const struct timespec wait_step = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
  int can_wait = 1;
  int sigpid = kill_group ? -pid : pid;
  int i;
  pid_t result;

  if (pid <= 0)
    return;

  result = waitpid(pid, NULL, WNOHANG);
  if (result == pid)
    return;
  if (result < 0 && errno == ECHILD)
    can_wait = 0;
  else if (result < 0)
    return;

  if (kill(sigpid, SIGTERM) < 0 && errno != ESRCH)
    return;

  if (!can_wait)
    goto sigkill;

  for (i = 0; i < 50; i++) {
    result = waitpid(pid, NULL, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD))
      return;
    if (result < 0 && errno != EINTR)
      return;
    nanosleep(&wait_step, NULL);
  }

sigkill:
  if (kill(sigpid, SIGKILL) < 0 && errno != ESRCH)
    return;

  if (!can_wait)
    return;

  for (i = 0; i < 50; i++) {
    result = waitpid(pid, NULL, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD))
      return;
    if (result < 0 && errno != EINTR)
      return;
    nanosleep(&wait_step, NULL);
  }
}

static inline int
client_is_x11(Client *c) {
#ifdef XWAYLAND
  return c->type == X11;
#endif
  return 0;
}

static Client *
focus_fallback_from(Client *anchor, Monitor *m) {
  Client *c;
  struct wl_list *l;
  for (l = anchor->link.prev; l != &clients; l = l->prev) {
    c = wl_container_of(l, c, link);
    if (VISIBLEON(c, m))
      return c;
  }
  return NULL;
}

static inline struct wlr_surface *
client_surface(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return c->surface.xwayland->surface;
#endif
  return c->surface.xdg->surface;
}

static inline int
toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc, LayerSurface **pl) {
  struct wlr_xdg_surface *xdg_surface, *tmp_xdg_surface;
  struct wlr_surface *root_surface;
  struct wlr_layer_surface_v1 *layer_surface;
  Client *c = NULL;
  LayerSurface *l = NULL;
  int type = -1;
#ifdef XWAYLAND
  struct wlr_xwayland_surface *xsurface;
#endif

  if (!s)
    return -1;
  root_surface = wlr_surface_get_root_surface(s);

#ifdef XWAYLAND
  if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(root_surface))) {
    c = xsurface->data;
    type = c->type;
    goto end;
  }
#endif

  if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root_surface))) {
    l = layer_surface->data;
    type = LayerShell;
    goto end;
  }

  xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);
  while (xdg_surface) {
    tmp_xdg_surface = NULL;
    switch (xdg_surface->role) {
    case WLR_XDG_SURFACE_ROLE_POPUP:
      if (!xdg_surface->popup || !xdg_surface->popup->parent)
        return -1;

      tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);

      if (!tmp_xdg_surface)
        return toplevel_from_wlr_surface(xdg_surface->popup->parent, pc, pl);

      xdg_surface = tmp_xdg_surface;
      break;
    case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
      c = xdg_surface->data;
      type = c->type;
      goto end;
    case WLR_XDG_SURFACE_ROLE_NONE:
      return -1;
    }
  }

end:
  if (pl)
    *pl = l;
  if (pc)
    *pc = c;
  return type;
}

static inline void
client_activate_surface(struct wlr_surface *s, int activated) {
  struct wlr_xdg_toplevel *toplevel;
#ifdef XWAYLAND
  struct wlr_xwayland_surface *xsurface;

  if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(s))) {
    wlr_xwayland_surface_activate(xsurface, activated);
    return;
  }
#endif
  if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(s)))
    wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline const char *
client_get_appid(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return c->surface.xwayland->class ? c->surface.xwayland->class : "broken";
#endif
  return c->surface.xdg->toplevel->app_id ? c->surface.xdg->toplevel->app_id : "broken";
}

static inline void
client_get_clip(Client *c, struct wlr_box *clip, struct wlr_box *geom) {
  *clip = (struct wlr_box){
      .x = 0,
      .y = 0,
      .width = MAX(geom->width, 1 + (int)c->bw) - c->bw,
      .height = MAX(geom->height, 1 + (int)c->bw) - c->bw,
  };

#ifdef XWAYLAND
  if (client_is_x11(c))
    return;
#endif

  clip->x = c->surface.xdg->geometry.x;
  clip->y = c->surface.xdg->geometry.y;
}

static inline void
client_get_geometry(Client *c, struct wlr_box *geom) {
#ifdef XWAYLAND
  if (client_is_x11(c)) {
    geom->x = c->surface.xwayland->x;
    geom->y = c->surface.xwayland->y;
    geom->width = c->surface.xwayland->width;
    geom->height = c->surface.xwayland->height;
    return;
  }
#endif
  *geom = c->surface.xdg->geometry;
}

static inline Client *
client_get_parent(Client *c) {
  Client *p = NULL;

#ifdef XWAYLAND
  if (client_is_x11(c)) {
    if (c->surface.xwayland->parent)
      toplevel_from_wlr_surface(c->surface.xwayland->parent->surface, &p, NULL);
    return p;
  }
#endif
  if (c->surface.xdg->toplevel->parent)
    toplevel_from_wlr_surface(c->surface.xdg->toplevel->parent->base->surface, &p, NULL);
  return p;
}

static inline int
client_has_children(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return !wl_list_empty(&c->surface.xwayland->children);
#endif
  return wl_list_length(&c->surface.xdg->link) > 1;
}

static inline const char *
client_get_title(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return c->surface.xwayland->title ? c->surface.xwayland->title : "broken";
#endif
  return c->surface.xdg->toplevel->title ? c->surface.xdg->toplevel->title : "broken";
}

static inline int
client_is_dialog_type(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c)) {
    struct wlr_xwayland_surface *surface = c->surface.xwayland;

    return surface->parent || surface->modal ||
           wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG) ||
           wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH) ||
           wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR) ||
           wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY);
  }
#endif
  return c->surface.xdg->toplevel->parent != NULL;
}

static inline int
client_is_float_type(Client *c) {
  struct wlr_xdg_toplevel *toplevel;
  struct wlr_xdg_toplevel_state state;

#ifdef XWAYLAND
  if (client_is_x11(c)) {
    struct wlr_xwayland_surface *surface = c->surface.xwayland;
    xcb_size_hints_t *size_hints = surface->size_hints;

    if (client_is_dialog_type(c))
      return 1;

    return size_hints && size_hints->min_width > 0 && size_hints->min_height > 0 && (size_hints->max_width == size_hints->min_width || size_hints->max_height == size_hints->min_height);
  }
#endif

  toplevel = c->surface.xdg->toplevel;
  state = toplevel->current;
  return client_is_dialog_type(c) || (state.min_width != 0 && state.min_height != 0 && (state.min_width == state.max_width || state.min_height == state.max_height));
}

static inline int
client_is_unmanaged(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return c->surface.xwayland->override_redirect;
#endif
  return 0;
}

static inline void
client_notify_enter(struct wlr_surface *s, struct wlr_keyboard *kb) {
  if (kb)
    wlr_seat_keyboard_notify_enter(seat, s, kb->keycodes, kb->num_keycodes,
                                   &kb->modifiers);
  else
    wlr_seat_keyboard_notify_enter(seat, s, NULL, 0, NULL);
}

static inline void
client_send_close(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c)) {
    wlr_xwayland_surface_close(c->surface.xwayland);
    return;
  }
#endif
  wlr_xdg_toplevel_send_close(c->surface.xdg->toplevel);
}

static inline void
scale_premultiplied_rgba(const float color[static 4], float opacity,
                         float out[static 4]) {
  out[0] = color[0] * opacity;
  out[1] = color[1] * opacity;
  out[2] = color[2] * opacity;
  out[3] = color[3] * opacity;
}

static inline void
client_set_border_color(Client *c, const float color[static 4]) {
  float applied_color[4];
  int i;

  scale_premultiplied_rgba(color, c->opacity, applied_color);
  for (i = 0; i < 4; i++) {
    if (c->border[i])
      wlr_scene_rect_set_color(c->border[i], applied_color);
  }
}

static inline void
client_set_fullscreen(Client *c, int fullscreen) {
#ifdef XWAYLAND
  if (client_is_x11(c)) {
    wlr_xwayland_surface_set_fullscreen(c->surface.xwayland, fullscreen);
    return;
  }
#endif
  wlr_xdg_toplevel_set_fullscreen(c->surface.xdg->toplevel, fullscreen);
}

static inline void
client_set_scale(struct wlr_surface *s, float scale) {
  wlr_fractional_scale_v1_notify_scale(s, scale);
  wlr_surface_set_preferred_buffer_scale(s, (int32_t)ceilf(scale));
}

static inline void
client_set_tiled(Client *c, uint32_t edges) {
#ifdef XWAYLAND
  if (client_is_x11(c)) {
    wlr_xwayland_surface_set_maximized(c->surface.xwayland,
                                       edges != WLR_EDGE_NONE,
                                       edges != WLR_EDGE_NONE);
    return;
  }
#endif
  if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >= XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION) {
    wlr_xdg_toplevel_set_tiled(c->surface.xdg->toplevel, edges);
  } else {
    wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel,
                                   edges != WLR_EDGE_NONE);
  }
}

static inline void
client_set_suspended(Client *c, int suspended) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return;
#endif

  wlr_xdg_toplevel_set_suspended(c->surface.xdg->toplevel, suspended);
}

static inline int
client_wants_focus(Client *c) {
#ifdef XWAYLAND
  return client_is_unmanaged(c) && wlr_xwayland_surface_override_redirect_wants_focus(c->surface.xwayland) && wlr_xwayland_surface_icccm_input_model(c->surface.xwayland) != WLR_ICCCM_INPUT_MODEL_NONE;
#endif
  return 0;
}

static inline int
client_wants_fullscreen(Client *c) {
#ifdef XWAYLAND
  if (client_is_x11(c))
    return c->surface.xwayland->fullscreen;
#endif
  return c->surface.xdg->toplevel->requested.fullscreen;
}

void swipe_begin(struct wl_listener *listener, void *data) {
  struct wlr_pointer_swipe_begin_event *event = data;

  wlr_pointer_gestures_v1_send_swipe_begin(
      pointer_gestures,
      seat,
      event->time_msec,
      event->fingers);
}

void swipe_update(struct wl_listener *listener, void *data) {
  struct wlr_pointer_swipe_update_event *event = data;

  wlr_pointer_gestures_v1_send_swipe_update(
      pointer_gestures,
      seat,
      event->time_msec,
      event->dx,
      event->dy);
}

void swipe_end(struct wl_listener *listener, void *data) {
  struct wlr_pointer_swipe_end_event *event = data;

  wlr_pointer_gestures_v1_send_swipe_end(
      pointer_gestures,
      seat,
      event->time_msec,
      event->cancelled);
}

void pinch_begin(struct wl_listener *listener, void *data) {
  struct wlr_pointer_pinch_begin_event *event = data;
  wlr_pointer_gestures_v1_send_pinch_begin(
      pointer_gestures,
      seat,
      event->time_msec,
      event->fingers);
}

void pinch_update(struct wl_listener *listener, void *data) {
  struct wlr_pointer_pinch_update_event *event = data;
  wlr_pointer_gestures_v1_send_pinch_update(
      pointer_gestures,
      seat,
      event->time_msec,
      event->dx,
      event->dy,
      event->scale,
      event->rotation);
}

void pinch_end(struct wl_listener *listener, void *data) {
  struct wlr_pointer_pinch_end_event *event = data;

  wlr_pointer_gestures_v1_send_pinch_end(
      pointer_gestures,
      seat,
      event->time_msec,
      event->cancelled);
}

void hold_begin(struct wl_listener *listener, void *data) {
  struct wlr_pointer_hold_begin_event *event = data;
  wlr_pointer_gestures_v1_send_hold_begin(
      pointer_gestures,
      seat,
      event->time_msec,
      event->fingers);
}

void hold_end(struct wl_listener *listener, void *data) {
  struct wlr_pointer_hold_end_event *event = data;
  wlr_pointer_gestures_v1_send_hold_end(
      pointer_gestures,
      seat,
      event->time_msec,
      event->cancelled);
}

void togglebar(const Arg *arg) {
  DwlIpcOutput *ipc_output;
  wl_list_for_each(ipc_output, &selmon->dwl_ipc_outputs, link)
      zdwl_ipc_output_v2_send_toggle_visibility(ipc_output->resource);
}

void dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *manager_resource = wl_resource_create(client, &zdwl_ipc_manager_v2_interface, version, id);
  if (!manager_resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(manager_resource, &dwl_manager_implementation, NULL, dwl_ipc_manager_destroy);

  zdwl_ipc_manager_v2_send_tags(manager_resource, TAGCOUNT);

  for (unsigned int i = 0; i < LENGTH(layouts); i++)
    zdwl_ipc_manager_v2_send_layout(manager_resource, layouts[i].symbol);
}

void dwl_ipc_manager_destroy(struct wl_resource *resource) {
}

void dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output) {
  DwlIpcOutput *ipc_output;
  Monitor *monitor = wlr_output_from_resource(output)->data;
  struct wl_resource *output_resource = wl_resource_create(client, &zdwl_ipc_output_v2_interface, wl_resource_get_version(resource), id);
  if (!output_resource)
    return;

  ipc_output = ecalloc(1, sizeof(*ipc_output));
  ipc_output->resource = output_resource;
  ipc_output->mon = monitor;
  wl_resource_set_implementation(output_resource, &dwl_output_implementation, ipc_output, dwl_ipc_output_destroy);
  wl_list_insert(&monitor->dwl_ipc_outputs, &ipc_output->link);
  dwl_ipc_output_printstatus_to(ipc_output);
}

void dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void dwl_ipc_output_destroy(struct wl_resource *resource) {
  DwlIpcOutput *ipc_output = wl_resource_get_user_data(resource);
  wl_list_remove(&ipc_output->link);
  free(ipc_output);
}

void dwl_ipc_output_printstatus(Monitor *monitor) {
  DwlIpcOutput *ipc_output;
  wl_list_for_each(ipc_output, &monitor->dwl_ipc_outputs, link)
      dwl_ipc_output_printstatus_to(ipc_output);
}

void dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output) {
  Monitor *monitor = ipc_output->mon;
  Client *c, *focused;
  int tagmask, state, numclients, focused_client, tag;
  const char *title, *appid;
  focused = focustop(monitor);
  zdwl_ipc_output_v2_send_active(ipc_output->resource, monitor == selmon);

  for (tag = 0; tag < TAGCOUNT; tag++) {
    numclients = state = focused_client = 0;
    tagmask = 1 << tag;
    if ((tagmask & monitor->tagset[monitor->seltags]) != 0)
      state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE;

    wl_list_for_each(c, &clients, link) {
      if (c->mon != monitor)
        continue;
      if (!(c->tags & tagmask))
        continue;
      if (c == focused)
        focused_client = 1;
      if (c->isurgent)
        state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT;

      numclients++;
    }
    zdwl_ipc_output_v2_send_tag(ipc_output->resource, tag, state, numclients, focused_client);
  }
  title = focused ? client_get_title(focused) : "";
  appid = focused ? client_get_appid(focused) : "";

  zdwl_ipc_output_v2_send_layout(ipc_output->resource, monitor->lt[monitor->sellt] - layouts);
  zdwl_ipc_output_v2_send_title(ipc_output->resource, title);
  zdwl_ipc_output_v2_send_appid(ipc_output->resource, appid);
  zdwl_ipc_output_v2_send_layout_symbol(ipc_output->resource, monitor->ltsymbol);
  if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FULLSCREEN_SINCE_VERSION) {
    zdwl_ipc_output_v2_send_fullscreen(ipc_output->resource, focused ? focused->isfullscreen : 0);
  }
  if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FLOATING_SINCE_VERSION) {
    zdwl_ipc_output_v2_send_floating(ipc_output->resource, focused ? focused->isfloating : 0);
  }
  zdwl_ipc_output_v2_send_frame(ipc_output->resource);
}

void dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags) {
  DwlIpcOutput *ipc_output;
  Monitor *monitor;
  Client *selected_client;
  unsigned int newtags = 0;

  ipc_output = wl_resource_get_user_data(resource);
  if (!ipc_output)
    return;

  monitor = ipc_output->mon;
  selected_client = focustop(monitor);
  if (!selected_client)
    return;

  newtags = (selected_client->tags & and_tags) ^ xor_tags;
  if (!newtags)
    return;

  selected_client->tags = newtags;
  if (selmon == monitor)
    focusclient(focustop(monitor), 1);
  arrange(monitor);
  printstatus();
}

void dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index) {
  DwlIpcOutput *ipc_output;
  Monitor *monitor;

  ipc_output = wl_resource_get_user_data(resource);
  if (!ipc_output)
    return;

  monitor = ipc_output->mon;
  if (index >= LENGTH(layouts))
    return;
  if (index != monitor->lt[monitor->sellt] - layouts)
    monitor->sellt ^= 1;

  monitor->lt[monitor->sellt] = &layouts[index];
  arrange(monitor);
  printstatus();
}

void dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset) {
  DwlIpcOutput *ipc_output;
  Monitor *monitor;
  unsigned int newtags = tagmask & TAGMASK;

  ipc_output = wl_resource_get_user_data(resource);
  if (!ipc_output)
    return;
  monitor = ipc_output->mon;
  setmonitortags(monitor, newtags, toggle_tagset);
}

void dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void setgaps(int o) {
  if (selmon) {
    selmon->gaps = MAX(o, 0);
    arrange(selmon);
  }
}

void togglegaps(const Arg *arg) {
  enablegaps = !enablegaps;
  arrange(selmon);
}

void defaultgaps(const Arg *arg) {
  setgaps(gaps);
}

void incgaps(const Arg *arg) {
  setgaps(selmon->gaps + arg->i);
}

float get_x_for_t(const struct BezierCurve *curve, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1 - t;
  float mt2 = mt * mt;

  return 3 * t * mt2 * curve->control_points[1].x +
         3 * t2 * mt * curve->control_points[2].x +
         t3 * curve->control_points[3].x;
}

float get_y_for_t(const struct BezierCurve *curve, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1 - t;
  float mt2 = mt * mt;

  return 3 * t * mt2 * curve->control_points[1].y +
         3 * t2 * mt * curve->control_points[2].y +
         t3 * curve->control_points[3].y;
}

float get_y_for_point(const struct BezierCurve *curve, float x) {
  int index, below, step, lower_index;
  float delta_x, perc_in_delta;
  const struct Vec2 *lower_point, *upper_point;

  if (x >= 1.0f)
    return 1.0f;
  if (x <= 0.0f)
    return 0.0f;

  index = 0;
  below = 1;

  for (step = (BAKED_POINTS + 1) / 2; step > 0; step /= 2) {
    if (below)
      index += step;
    else
      index -= step;

    below = curve->baked_points[index].x < x;
  }

  lower_index = index - (!below || index == BAKED_POINTS - 1);

  lower_point = &curve->baked_points[lower_index];
  upper_point = &curve->baked_points[lower_index + 1];

  delta_x = upper_point->x - lower_point->x;
  if (delta_x == 0.0f)
    return lower_point->y;

  perc_in_delta = (x - lower_point->x) / delta_x;
  return lower_point->y + (upper_point->y - lower_point->y) * perc_in_delta;
}

static void
sample_animation_timing(const struct timespec *start_time, const struct timespec *now,
                        float *progress, float *eased_progress) {
  *progress = ((now->tv_sec - start_time->tv_sec) * 1000 +
               (now->tv_nsec - start_time->tv_nsec) / 1000000) /
              ANIMATION_DURATION;
  *eased_progress = get_y_for_point(&bezier, *progress);
}

static void
sample_animation(const Animation *anim, float t, struct wlr_box *geom, float *opacity) {
  if (geom) {
    geom->x = (int)(anim->start.x + (anim->target.x - anim->start.x) * t);
    geom->y = (int)(anim->start.y + (anim->target.y - anim->start.y) * t);
    geom->width = (int)(anim->start.width + (anim->target.width - anim->start.width) * t);
    geom->height = (int)(anim->start.height + (anim->target.height - anim->start.height) * t);
  }

  if (opacity)
    *opacity = anim->start_opacity + (anim->target_opacity - anim->start_opacity) * t;
}

static void
sample_animation_state(const Animation *anim, const struct timespec *now,
                       struct wlr_box *geom, float *opacity,
                       float *progress, float *eased_progress) {
  float local_progress, local_eased_progress;
  struct timespec local_now;

  if (!now) {
    clock_gettime(CLOCK_MONOTONIC, &local_now);
    now = &local_now;
  }

  sample_animation_timing(&anim->start_time, now, &local_progress,
                          &local_eased_progress);
  sample_animation(anim, local_eased_progress, geom, opacity);
  if (progress)
    *progress = local_progress;
  if (eased_progress)
    *eased_progress = local_eased_progress;
}

static int
client_is_switch_exiting(Client *c) {
  return c->anim.active && c->hide_on_anim_end;
}

static void
get_client_settled_geometry(Client *c, struct wlr_box *geom) {
  if (client_is_switch_exiting(c)) {
    *geom = c->restore_geom;
    return;
  }

  if (c->anim.active) {
    *geom = c->anim.target;
    return;
  }

  *geom = c->geom;
}

static int
get_monitor_switch_offset(Monitor *m, uint32_t tags) {
  Client *c;
  struct wlr_box current_geom, settled_geom;
  int offset_sum = 0, samples = 0;

  wl_list_for_each(c, &clients, link) {
    if (c->mon != m || !VISIBLEONTAGS(c, m, tags))
      continue;
    if (!c->is_tag_switch_anim || !c->anim.active || c->hide_on_anim_end)
      continue;

    sample_animation_state(&c->anim, &m->switch_start_time, &current_geom, NULL, NULL, NULL);
    get_client_settled_geometry(c, &settled_geom);
    offset_sum += current_geom.x - settled_geom.x;
    samples++;
  }

  return samples ? offset_sum / samples : 0;
}

static void
get_layer_surface_geom(LayerSurface *l, struct wlr_box *geom) {
  int x, y;

  wlr_scene_node_coords(&l->scene->node, &x, &y);
  geom->x = x;
  geom->y = y;
  geom->width = MAX((int)l->layer_surface->current.actual_width, 1);
  geom->height = MAX((int)l->layer_surface->current.actual_height, 1);
}

static void
set_layer_surface_geom(LayerSurface *l, const struct wlr_box *geom) {
  l->geom = *geom;
  wlr_scene_node_set_position(&l->scene->node, geom->x, geom->y);
  wlr_scene_node_set_position(&l->popups->node, geom->x, geom->y);
}

static void
set_scene_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
  float opacity = *(float *)data;

  (void)sx;
  (void)sy;

  wlr_scene_buffer_set_opacity(buffer, opacity);
}

static const float *
client_border_base_color(Client *c) {
  struct wlr_surface *surface = client_surface(c);

  if (c->isurgent)
    return urgentcolor;
  if (surface && seat && surface == seat->keyboard_state.focused_surface && !exclusive_focus && !seat->drag)
    return focuscolor;
  return bordercolor;
}

static void
update_client_border_color(Client *c) {
  if (!c->border[0])
    return;
  client_set_border_color(c, client_border_base_color(c));
}

static void
apply_client_opacity(Client *c, float opacity) {
  if (!c->scene)
    return;

  c->opacity = fmaxf(0.0f, fminf(opacity, 1.0f));
  wlr_scene_node_for_each_buffer(&c->scene->node, set_scene_buffer_opacity,
                                 &c->opacity);
  update_client_border_color(c);
}

static void
apply_layer_surface_opacity(LayerSurface *l, float opacity) {
  if (!l->scene)
    return;

  l->opacity = fmaxf(0.0f, fminf(opacity, 1.0f));
  wlr_scene_node_for_each_buffer(&l->scene->node, set_scene_buffer_opacity,
                                 &l->opacity);
  wlr_scene_node_for_each_buffer(&l->popups->node, set_scene_buffer_opacity,
                                 &l->opacity);
}

static void
get_projected_animation_box(const Animation *anim, struct wlr_box *projected) {
  *projected = anim->target;
  if (bezier_max_y <= 1.0f)
    return;

  if (anim->target.width != anim->start.width)
    projected->width = (int)(anim->start.width + (anim->target.width - anim->start.width) * bezier_max_y);
  if (anim->target.height != anim->start.height)
    projected->height = (int)(anim->start.height + (anim->target.height - anim->start.height) * bezier_max_y);
}

static void
client_apply_visual_geometry(Client *c, const struct wlr_box *geo) {
  struct wlr_box clip;

  if (!c->scene || !c->scene_surface)
    return;

  wlr_scene_node_set_position(&c->scene->node, geo->x, geo->y);
  wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
  if (c->border[0]) {
    wlr_scene_rect_set_size(c->border[0], MAX(geo->width, 1 + (int)c->bw), c->bw);
    wlr_scene_rect_set_size(c->border[1], MAX(geo->width, 1 + (int)c->bw), c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw,
                            MAX(geo->height, 1 + 2 * (int)c->bw) - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw,
                            MAX(geo->height, 1 + 2 * (int)c->bw) - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0,
                                MAX(geo->height, 1 + (int)c->bw) - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(&c->border[3]->node,
                                MAX(geo->width, 1 + (int)c->bw) - c->bw, c->bw);
  }
  client_get_clip(c, &clip, (struct wlr_box *)geo);
  wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

static void
client_apply_scene_geometry(Client *c, const struct wlr_box *geo) {
  c->current_geom = *geo;
  client_apply_visual_geometry(c, geo);
}

static int
client_is_interactive_resize(Client *c) {
  return cursor_mode == CurResize && grabc == c;
}

static int
client_should_use_visual_geometry(Client *c, const struct wlr_box *current,
                                  const struct wlr_box *visual) {
  if (c->anim.active)
    return 1;

  if (client_is_interactive_resize(c))
    return 0;

  return current->width > visual->width || current->height > visual->height;
}

static void
client_apply_current_visual_geometry(Client *c, const struct wlr_box *current,
                                     const struct wlr_box *visual, int immediate_scene) {
  c->current_geom = *current;
  client_apply_visual_geometry(c, (!immediate_scene || client_should_use_visual_geometry(c, current, visual))
                                      ? visual
                                      : current);
}

static void
client_apply_sampled_opacity(Client *c) {
  float opacity;

  if (!c->scene)
    return;

  sample_animation_state(&c->anim, NULL, NULL, &opacity, NULL, NULL);
  apply_client_opacity(c, opacity);
}

static void
client_init_common(Client *c, unsigned int type, unsigned int bw,
                   unsigned int initial_position) {
  c->type = type;
  c->bw = bw;
  c->opacity = 1.0f;
  c->initial_position = initial_position;
  wl_list_init(&c->link);
  wl_list_init(&c->commit.link);
  wl_list_init(&c->map.link);
  wl_list_init(&c->unmap.link);
}

static void
client_apply_committed_geometry(Client *c, int width, int height) {
  struct wlr_box committed;

  committed = c->current_geom.width > 0 && c->current_geom.height > 0
                  ? c->current_geom
                  : c->geom;

  if (committed.x != c->geom.x && committed.x + committed.width == c->geom.x + c->geom.width)
    committed.x = c->geom.x + c->geom.width - width;
  else
    committed.x = c->geom.x;

  if (committed.y != c->geom.y && committed.y + committed.height == c->geom.y + c->geom.height)
    committed.y = c->geom.y + c->geom.height - height;
  else
    committed.y = c->geom.y;

  committed.width = width;
  committed.height = height;
  client_apply_current_visual_geometry(c, &committed, &c->geom, 1);
}

static void
client_get_requested_surface_size(Client *c, const struct wlr_box *geo,
                                  const struct wlr_box *prev,
                                  uint32_t *width, uint32_t *height) {
  *width = MAX(c->anim.active ? prev && c->anim.projected.width > c->anim.target.width && geo->width < prev->width
                                    ? MAX(geo->width, c->anim.target.width)
                                    : MAX(geo->width,
                                          MAX(c->anim.target.width, c->anim.projected.width))
                              : geo->width,
               1 + 2 * (int)c->bw) -
           2 * c->bw;
  *height = MAX(c->anim.active ? prev && c->anim.projected.height > c->anim.target.height && geo->height < prev->height
                                     ? MAX(geo->height, c->anim.target.height)
                                     : MAX(geo->height,
                                           MAX(c->anim.target.height, c->anim.projected.height))
                               : geo->height,
                1 + 2 * (int)c->bw) -
            2 * c->bw;
}

#ifdef XWAYLAND
static void
client_configure_x11_surface(Client *c, const struct wlr_box *geo,
                             uint32_t width, uint32_t height) {
  wlr_xwayland_surface_configure(c->surface.xwayland,
                                 geo->x + c->bw, geo->y + c->bw, width, height);
}
#endif

static void
client_clear_pending_configure(Client *c) {
  if (!c->pending_configure_timeout) {
    c->pending_configure_serial = 0;
    return;
  }

  wl_event_source_remove(c->pending_configure_timeout);
  c->pending_configure_timeout = NULL;
  c->pending_configure_serial = 0;
}

static int
client_handle_configure_timeout(void *data) {
  Client *c = data;
  if (!c->pending_configure_serial)
    return 0;

  client_clear_pending_configure(c);
  if (!c->scene || !client_surface(c)->mapped)
    return 0;

  if (c->geom.x != c->current_geom.x || c->geom.y != c->current_geom.y) {
    struct wlr_box moved = c->current_geom.width > 0 && c->current_geom.height > 0
                               ? c->current_geom
                               : c->geom;
    moved.x = c->geom.x;
    moved.y = c->geom.y;
    client_apply_current_visual_geometry(c, &moved, &c->geom, 1);
  }

  client_set_scheduled_size(
      c, client_should_use_visual_geometry(c, &c->current_geom, &c->geom) ? &c->geom
                                                                          : &c->current_geom);
  return 0;
}

static void
client_handle_x11_commit(Client *c) {
  if (c->scene && client_surface(c)->mapped) {
    struct wlr_box committed = {
        .x = c->surface.xwayland->x - (int)c->bw,
        .y = c->surface.xwayland->y - (int)c->bw,
        .width = MAX(c->surface.xwayland->width, 1) + 2 * c->bw,
        .height = MAX(c->surface.xwayland->height, 1) + 2 * c->bw,
    };

    if (client_is_unmanaged(c))
      client_apply_scene_geometry(c, &committed);
    else
      client_apply_committed_geometry(c, committed.width, committed.height);

    if (!c->anim.active && !client_should_use_visual_geometry(c, &c->current_geom, &c->geom))
      c->geom = c->current_geom;
  }

  client_apply_sampled_opacity(c);
}

static int
client_get_xdg_committed_size(Client *c, struct wlr_box *size) {
  int inner_width, inner_height;

  *size = c->surface.xdg->geometry;
  if (size->width <= 0 || size->height <= 0) {
    size->width = c->surface.xdg->surface->current.width;
    size->height = c->surface.xdg->surface->current.height;
  }

  if (size->width <= 0 || size->height <= 0)
    return 0;

  inner_width = MAX(c->geom.width, 1 + 2 * (int)c->bw) - 2 * c->bw;
  inner_height = MAX(c->geom.height, 1 + 2 * (int)c->bw) - 2 * c->bw;
  if ((size->width != inner_width || size->height != inner_height) && c->surface.xdg->surface->current.width == inner_width && c->surface.xdg->surface->current.height == inner_height) {
    size->width = c->surface.xdg->surface->current.width;
    size->height = c->surface.xdg->surface->current.height;
  }

  return 1;
}

static void
client_set_scheduled_size(Client *c, const struct wlr_box *geo) {
  if (client_is_x11(c))
    return;

  c->surface.xdg->toplevel->scheduled.width =
      MAX(geo->width, 1 + 2 * (int)c->bw) - 2 * c->bw;
  c->surface.xdg->toplevel->scheduled.height =
      MAX(geo->height, 1 + 2 * (int)c->bw) - 2 * c->bw;
}

static void
client_set_pending_configure_serial(Client *c, uint32_t serial) {
  if (!serial)
    return;

  c->pending_configure_serial = serial;
  if (!c->pending_configure_timeout) {
    c->pending_configure_timeout =
        wl_event_loop_add_timer(event_loop, client_handle_configure_timeout, c);
  }
  wl_event_source_timer_update(c->pending_configure_timeout, CONFIGURE_TIMEOUT_MS);
}

static void
client_create_scene(Client *c) {
  c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
  wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
  c->scene_surface = c->type == XDGShell
                         ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
                         : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
  c->scene->node.data = c->scene_surface->node.data = c;
  apply_client_opacity(c, c->opacity);
  if (c->type == XDGShell) {
    wl_list_remove(&c->commit.link);
    wl_list_init(&c->commit.link);
    LISTEN(&client_surface(c)->events.commit, &c->commit, commitnotify);
  }
}

static void
client_create_borders(Client *c) {
  float initial_color[4];
  int i;

  scale_premultiplied_rgba(c->isurgent ? urgentcolor : bordercolor,
                           c->opacity, initial_color);
  for (i = 0; i < 4; i++) {
    c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, initial_color);
    c->border[i]->node.data = c;
  }
  update_client_border_color(c);
}

static void
client_bootstrap_geometry(Client *c) {
  client_get_geometry(c, &c->current_geom);
  c->geom = c->current_geom;

  if (client_wants_fullscreen(c))
    setfullscreen(c, 1);
}

static int
client_map_unmanaged(Client *c) {
  if (!client_is_unmanaged(c))
    return 0;

  wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
  client_apply_scene_geometry(c, &c->current_geom);
  if (client_wants_focus(c)) {
    focusclient(c, 1);
    exclusive_focus = c;
  }
  return 1;
}

static void
client_finish_managed_map(Client *c, Client *parent) {
  client_create_borders(c);
  client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
  wl_list_insert(clients.prev, &c->link);

  if (parent) {
    c->isfloating = 1;
    if (parent->mon) {
      c->geom.x = (parent->mon->w.width - c->geom.width) / 2 + parent->mon->m.x;
      c->geom.y = (parent->mon->w.height - c->geom.height) / 2 + parent->mon->m.y;
    }
    setmon(c, parent->mon, parent->tags);
  } else {
    applyrules(c);
    focusclient(c, 1);
  }
}

static void
client_apply_x11_configure_request(
    Client *c, struct wlr_xwayland_surface_configure_event *event,
    const struct wlr_box *geo) {
  if (!client_surface(c) || !client_surface(c)->mapped) {
    wlr_xwayland_surface_configure(c->surface.xwayland,
                                   event->x, event->y, event->width, event->height);
    return;
  }

  if (client_is_unmanaged(c)) {
    c->geom = *geo;
    client_apply_scene_geometry(c, geo);
    wlr_xwayland_surface_configure(c->surface.xwayland,
                                   event->x, event->y, event->width, event->height);
    return;
  }

  if ((c->isfloating && c != grabc) || !c->mon->lt[c->mon->sellt]->arrange)
    client_request_geometry(c, geo, 0);
  else
    arrange(c->mon);
}

static void
client_request_surface_size(Client *c, const struct wlr_box *geo,
                            const struct wlr_box *prev) {
  uint32_t width, height;

  if (!c->mon || !c->scene || !client_surface(c)->mapped || client_is_unmanaged(c))
    return;

  client_get_requested_surface_size(c, geo, prev, &width, &height);

#ifdef XWAYLAND
  if (client_is_x11(c)) {
    client_configure_x11_surface(c, geo, width, height);
    return;
  }
#endif

  client_set_pending_configure_serial(c,
                                      (c->pending_configure_serial && c->surface.xdg->toplevel->scheduled.width == (int32_t)width && c->surface.xdg->toplevel->scheduled.height == (int32_t)height)
                                          ? 0
                                          : wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel,
                                                                      (int32_t)width,
                                                                      (int32_t)height));
}

static void
client_request_geometry(Client *c, const struct wlr_box *geo, int interact) {
  struct wlr_box prev = c->geom;
  int size_changed;
  int has_current;

  if (interact)
    c->anim.active = 0;

  c->geom = *geo;
  if (!c->mon || !c->scene || !client_surface(c)->mapped)
    return;

  if (client_is_unmanaged(c)) {
    client_apply_scene_geometry(c, geo);
    return;
  }

  size_changed = geo->width != prev.width || geo->height != prev.height;
  has_current = c->current_geom.width > 0 && c->current_geom.height > 0;

#ifdef XWAYLAND
  if (client_is_x11(c)) {
    uint32_t width, height;

    client_get_requested_surface_size(c, geo, size_changed ? &prev : NULL,
                                      &width, &height);
    client_configure_x11_surface(c, geo, width, height);
    if (!size_changed) {
      struct wlr_box current = has_current ? c->current_geom : *geo;
      current.x = geo->x;
      current.y = geo->y;
      client_apply_current_visual_geometry(c, &current, geo, interact);
    }
    return;
  }
#endif

  if (size_changed) {
    client_request_surface_size(c, geo, &prev);
  } else if (!c->pending_configure_serial) {
    struct wlr_box current = has_current ? c->current_geom : *geo;
    current.x = geo->x;
    current.y = geo->y;
    client_apply_current_visual_geometry(c, &current, geo, interact);
  }
}

static void
start_animation_at(Client *c, const struct wlr_box *target, const struct wlr_box *start,
                   float target_opacity, const struct timespec *start_time) {
  int pre_resize = 0;

  if (!start && c->anim.active && wlr_box_equal(&c->anim.target, target) && c->anim.target_opacity == target_opacity) {
    if (c->mon && c->mon->wlr_output)
      wlr_output_schedule_frame(c->mon->wlr_output);
    return;
  }

  c->anim.start = start ? *start : c->geom;
  c->anim.target = *target;
  c->anim.start_opacity = c->opacity;
  c->anim.target_opacity = target_opacity;
  c->anim.projected = *target;
  get_projected_animation_box(&c->anim, &c->anim.projected);
  pre_resize = c->anim.projected.width > c->anim.target.width || c->anim.projected.height > c->anim.target.height;

  if (wlr_box_equal(&c->anim.start, target) && c->anim.start_opacity == c->anim.target_opacity) {
    c->anim.active = 0;
    apply_client_opacity(c, c->anim.target_opacity);
    return;
  }

  if (start_time)
    c->anim.start_time = *start_time;
  else
    clock_gettime(CLOCK_MONOTONIC, &c->anim.start_time);
  c->anim.active = 1;
  apply_client_opacity(c, c->anim.start_opacity);
  if (pre_resize)
    client_request_surface_size(c, &c->anim.projected, NULL);

  if (c->mon && c->mon->wlr_output)
    wlr_output_schedule_frame(c->mon->wlr_output);
}

static int
client_has_initial_position(Client *c, Monitor *m) {
  return (c->initial_position && m && m->lt[m->sellt]->arrange &&
          !c->isfloating && !c->isfullscreen) ||
         (c->initial_position && client_is_float_type(c));
}

static void
apply_initial_box_offset(struct wlr_box *box, int dir_x, int dir_y) {
  int offset_x = MAX(box->width / 5, 24);
  int offset_y = MAX(box->height / 3, 24);

  box->x += dir_x * offset_x;
  box->y += dir_y * offset_y;
}

static int
prepare_initial_client_position(Client *c, Monitor *m, const struct wlr_box *target,
                                struct wlr_box *start) {
  if (!client_has_initial_position(c, m))
    return 0;

  *start = *target;
  if (!c->isfloating && !c->isfullscreen && m && m->lt[m->sellt]->arrange &&
      initial_layout_animation_scale > 0.0f) {
    start->width = (int)(target->width * initial_layout_animation_scale);
    start->height = (int)(target->height * initial_layout_animation_scale);
    start->x = target->x + (target->width - start->width) / 2;
    start->y = target->y + (target->height - start->height) / 2;
  } else if (client_is_float_type(c)) {
    apply_initial_box_offset(start, 0, 1);
  }

  c->opacity = 0.0f;
  c->initial_position = 0;
  return 1;
}

static void
start_layout_animation(Client *c, Monitor *m, const struct wlr_box *target) {
  struct wlr_box start;
  int has_initial_start;

  if (c->close_pending)
    return;

  has_initial_start = prepare_initial_client_position(c, m, target, &start);

  if (m->switch_animate && !VISIBLEONTAGS(c, m, m->prevtagset)) {
    start = *target;
    start.x += m->switch_dir * m->m.width + m->switch_offset;
    c->hide_on_anim_end = 0;
    c->is_tag_switch_anim = 1;
    client_apply_visual_geometry(c, &start);
    start_animation_at(c, target, &start, 1.0f, &m->switch_start_time);
    return;
  }

  c->is_tag_switch_anim = 0;
  if (has_initial_start) {
    client_apply_visual_geometry(c, &start);
    start_animation_at(c, target, &start, 1.0f, NULL);
    return;
  }

  start_animation_at(c, target, NULL, 1.0f, NULL);
}

static void
start_tag_switch_exit_animation(Client *c, Monitor *m) {
  struct wlr_box restore, start, target;
  float start_opacity;
  sample_animation_state(&c->anim, &m->switch_start_time, &start, &start_opacity, NULL, NULL);
  get_client_settled_geometry(c, &restore);
  c->opacity = start_opacity;
  c->restore_geom = restore;
  c->hide_on_anim_end = 1;
  c->is_tag_switch_anim = 1;

  target = restore;
  target.x -= m->switch_dir * m->m.width;
  client_apply_visual_geometry(c, &start);
  start_animation_at(c, &target, &start, start_opacity, &m->switch_start_time);
}

static void
prepare_layer_surface_initial_position(LayerSurface *l, const struct wlr_box *target,
                                       struct wlr_box *start) {
  uint32_t anchor = l->layer_surface->current.anchor;
  int dir_x = 0;
  int dir_y = 0;

  *start = *target;
  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    dir_y = -1;
  else if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP))
    dir_y = 1;
  else if (!(anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)))
    dir_y = 1;

  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    dir_x = -1;
  else if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT))
    dir_x = 1;

  apply_initial_box_offset(start, dir_x, dir_y);
}

static void
start_layer_surface_animation(LayerSurface *l, const struct wlr_box *target,
                              const struct wlr_box *start,
                              float start_opacity, float target_opacity) {
  l->anim.start = start ? *start : l->geom;
  l->anim.target = *target;
  l->anim.start_opacity = start_opacity;
  l->anim.target_opacity = target_opacity;

  if (wlr_box_equal(&l->anim.start, target) && l->anim.start_opacity == l->anim.target_opacity) {
    l->anim.active = 0;
    set_layer_surface_geom(l, target);
    apply_layer_surface_opacity(l, target_opacity);
    return;
  }

  clock_gettime(CLOCK_MONOTONIC, &l->anim.start_time);
  l->anim.active = 1;
  set_layer_surface_geom(l, &l->anim.start);
  apply_layer_surface_opacity(l, start_opacity);

  if (l->mon && l->mon->wlr_output)
    wlr_output_schedule_frame(l->mon->wlr_output);
}

static void
retarget_layer_surface(LayerSurface *l, const struct wlr_box *target) {
  struct wlr_box start;
  float start_opacity;

  if (!l->mapped) {
    set_layer_surface_geom(l, target);
    apply_layer_surface_opacity(l, 1.0f);
    return;
  }

  if (l->initial_position) {
    prepare_layer_surface_initial_position(l, target, &start);
    start_opacity = 0.0f;
    l->initial_position = 0;
  } else if (l->anim.active) {
    sample_animation_state(&l->anim, NULL, &start, &start_opacity, NULL, NULL);
  } else {
    start = l->geom;
    start_opacity = l->opacity;
  }

  start_layer_surface_animation(l, target, &start, start_opacity, 1.0f);
}

static void
snapshot_close_overlay_buffer(struct wlr_scene_buffer *buffer, int sx, int sy,
                              void *data) {
  CloseOverlaySnapshotData *snapshot_data = data;
  CloseOverlay *overlay = snapshot_data->overlay;
  Client *c = snapshot_data->client;
  CloseOverlayBuffer *snapshot;
  int i;

  if (!buffer->buffer)
    return;
  for (i = 0; c && i < 4; i++) {
    if (c->border[i] && &buffer->node == &c->border[i]->node)
      return;
  }

  snapshot = ecalloc(1, sizeof(*snapshot));
  snapshot->scene_buffer = wlr_scene_buffer_create(overlay->scene, buffer->buffer);
  snapshot->opacity = buffer->opacity;
  wlr_scene_node_set_position(&snapshot->scene_buffer->node,
                              sx - overlay->geom.x, sy - overlay->geom.y);
  if (buffer->src_box.width > 0.0f && buffer->src_box.height > 0.0f)
    wlr_scene_buffer_set_source_box(snapshot->scene_buffer, &buffer->src_box);
  if (buffer->dst_width > 0 && buffer->dst_height > 0)
    wlr_scene_buffer_set_dest_size(snapshot->scene_buffer,
                                   buffer->dst_width, buffer->dst_height);
  wlr_scene_buffer_set_transform(snapshot->scene_buffer, buffer->transform);
  wlr_scene_buffer_set_filter_mode(snapshot->scene_buffer, buffer->filter_mode);
  wlr_scene_buffer_set_opacity(snapshot->scene_buffer, snapshot->opacity);
  wl_list_insert(overlay->buffers.prev, &snapshot->link);
}

static void
snapshot_close_overlay_tree(CloseOverlay *overlay, struct wlr_scene_tree *scene_tree,
                            Client *client) {
  CloseOverlaySnapshotData snapshot_data;

  if (!scene_tree)
    return;

  snapshot_data.overlay = overlay;
  snapshot_data.client = client;
  wlr_scene_node_for_each_buffer(&scene_tree->node,
                                 snapshot_close_overlay_buffer, &snapshot_data);
}

static void
transfer_client_borders_to_close_overlay(CloseOverlay *overlay, Client *client) {
  CloseOverlayRect *rect;
  int i, x, y;

  if (!client)
    return;

  for (i = 0; i < 4; i++) {
    if (!client->border[i])
      continue;

    wlr_scene_node_coords(&client->border[i]->node, &x, &y);
    rect = ecalloc(1, sizeof(*rect));
    rect->scene_rect = client->border[i];
    memcpy(rect->color, client->border[i]->color, sizeof(rect->color));
    wlr_scene_node_reparent(&client->border[i]->node, overlay->scene);
    wlr_scene_node_set_position(&client->border[i]->node,
                                x - overlay->geom.x, y - overlay->geom.y);
    wl_list_insert(overlay->rects.prev, &rect->link);
    client->border[i] = NULL;
  }
}

static void
activate_close_overlay(CloseOverlay *overlay) {
  apply_close_overlay_opacity(overlay, 1.0f);
  clock_gettime(CLOCK_MONOTONIC, &overlay->start_time);
  wl_list_insert(close_overlays.prev, &overlay->link);
  if (overlay->mon && overlay->mon->wlr_output)
    wlr_output_schedule_frame(overlay->mon->wlr_output);
}

static CloseOverlay *
create_close_overlay_base(Monitor *mon, const struct wlr_box *geom) {
  CloseOverlay *overlay = ecalloc(1, sizeof(*overlay));

  overlay->mon = mon;
  overlay->geom = *geom;
  overlay->scene = wlr_scene_tree_create(close_overlay_tree);
  wl_list_init(&overlay->buffers);
  wl_list_init(&overlay->rects);
  wlr_scene_node_set_position(&overlay->scene->node, geom->x, geom->y);
  return overlay;
}

static void
destroy_close_overlay(CloseOverlay *overlay) {
  CloseOverlayBuffer *buffer, *tmp;
  CloseOverlayRect *rect, *rect_tmp;

  wl_list_remove(&overlay->link);
  wl_list_for_each_safe(buffer, tmp, &overlay->buffers, link) {
    wl_list_remove(&buffer->link);
    free(buffer);
  }
  wl_list_for_each_safe(rect, rect_tmp, &overlay->rects, link) {
    wl_list_remove(&rect->link);
    free(rect);
  }
  wlr_scene_node_destroy(&overlay->scene->node);
  free(overlay);
}

static void
apply_close_overlay_opacity(CloseOverlay *overlay, float opacity) {
  CloseOverlayBuffer *buffer;
  CloseOverlayRect *rect;
  float color[4];

  opacity = fmaxf(0.0f, fminf(opacity, 1.0f));
  wl_list_for_each(buffer, &overlay->buffers, link)
      wlr_scene_buffer_set_opacity(buffer->scene_buffer, buffer->opacity * opacity);
  wl_list_for_each(rect, &overlay->rects, link) {
    scale_premultiplied_rgba(rect->color, opacity, color);
    wlr_scene_rect_set_color(rect->scene_rect, color);
  }
}

static void
create_close_overlay(Client *c, const struct wlr_box *geom) {
  CloseOverlay *overlay = create_close_overlay_base(c->mon, geom);

  snapshot_close_overlay_tree(overlay, c->scene, c);
  transfer_client_borders_to_close_overlay(overlay, c);
  activate_close_overlay(overlay);
}

static void
create_layer_surface_close_overlay(LayerSurface *l, const struct wlr_box *geom) {
  CloseOverlay *overlay = create_close_overlay_base(l->mon, geom);

  snapshot_close_overlay_tree(overlay, l->scene, NULL);
  snapshot_close_overlay_tree(overlay, l->popups, NULL);
  activate_close_overlay(overlay);
}

static void
cancel_tag_switch_exit_animations(Monitor *m) {
  Client *c;

  wl_list_for_each(c, &clients, link) {
    if (c->mon != m || !client_is_switch_exiting(c))
      continue;

    c->anim.active = 0;
    c->hide_on_anim_end = 0;
    c->is_tag_switch_anim = 0;
    c->geom = c->restore_geom;
    client_apply_scene_geometry(c, &c->restore_geom);
    apply_client_opacity(c, 1.0f);
    wlr_scene_node_set_enabled(&c->scene->node, 0);
    client_set_suspended(c, 1);
  }
}

static void
start_close_animation(Client *c) {
  if (c->close_pending || !c->mon || !client_surface(c)->mapped) {
    client_send_close(c);
    return;
  }
  sample_animation_state(&c->anim, NULL, &c->geom, &c->opacity, NULL, NULL);
  client_apply_scene_geometry(c, &c->geom);
  apply_client_opacity(c, c->opacity);
  create_close_overlay(c, &c->geom);
  c->anim.active = 0;
  c->close_pending = 1;
  c->tags = 0;
  wlr_scene_node_set_enabled(&c->scene->node, 0);
  client_set_suspended(c, 1);
  client_send_close(c);
}

static int
step_client_animation_frame(Client *c, const struct timespec *now) {
  struct wlr_box geom;
  float opacity, progress;

  sample_animation_state(&c->anim, now, &geom, &opacity, &progress, NULL);
  if (progress >= 1.0f) {
    c->anim.active = 0;
    c->is_tag_switch_anim = 0;
    if (c->hide_on_anim_end) {
      c->hide_on_anim_end = 0;
      c->geom = c->restore_geom;
      client_apply_scene_geometry(c, &c->restore_geom);
      apply_client_opacity(c, 1.0f);
      wlr_scene_node_set_enabled(&c->scene->node, 0);
      client_set_suspended(c, 1);
      return 0;
    }

    c->geom = c->anim.target;
    if (c->pending_configure_serial || c->anim.projected.width != c->anim.target.width || c->anim.projected.height != c->anim.target.height) {
      client_request_surface_size(c, &c->anim.target, NULL);
    } else {
      client_request_geometry(c, &c->anim.target, 0);
    }
    client_apply_visual_geometry(c, &c->anim.target);
    apply_client_opacity(c, c->anim.target_opacity);
    return 0;
  }

  client_request_geometry(c, &geom, 0);
  client_apply_visual_geometry(c, &geom);
  apply_client_opacity(c, opacity);
  return 1;
}

static int
step_layer_surface_animation_frame(LayerSurface *l, const struct timespec *now) {
  struct wlr_box geom;
  float opacity, progress;

  sample_animation_state(&l->anim, now, &geom, &opacity, &progress, NULL);
  set_layer_surface_geom(l, &geom);
  apply_layer_surface_opacity(l, opacity);
  if (progress >= 1.0f) {
    l->anim.active = 0;
    set_layer_surface_geom(l, &l->anim.target);
    apply_layer_surface_opacity(l, l->anim.target_opacity);
    return 0;
  }

  return 1;
}

static int
step_close_overlay_frame(CloseOverlay *overlay, const struct timespec *now) {
  float progress, eased_progress;

  sample_animation_timing(&overlay->start_time, now, &progress, &eased_progress);
  if (progress >= 1.0f) {
    destroy_close_overlay(overlay);
    return 0;
  }

  apply_close_overlay_opacity(overlay, 1.0f - eased_progress);
  return 1;
}

void init_bezier(void) {
  bezier_max_y = 1.0f;
  for (int i = 0; i < BAKED_POINTS; ++i) {
    float t = (i + 1) * INV_BAKED_POINTS;
    bezier.baked_points[i].x = get_x_for_t(&bezier, t);
    bezier.baked_points[i].y = get_y_for_t(&bezier, t);
    bezier_max_y = fmaxf(bezier_max_y, bezier.baked_points[i].y);
  }
}

void pushlasttop(const Arg *arg) {
  Client *c, *last = NULL;

  if (!selmon)
    return;

  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
      continue;
    last = c;
  }

  if (last) {
    wl_list_remove(&last->link);
    wl_list_insert(&clients, &last->link);
  }
  arrange(selmon);
}

void pushfirstbottom(const Arg *arg) {
  Client *c, *first = NULL;

  if (!selmon)
    return;

  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
      continue;
    first = c;
    break;
  }

  if (first) {
    wl_list_remove(&first->link);
    wl_list_insert(clients.prev, &first->link);
  }
  arrange(selmon);
}

void applyrules(Client *c) {
  const char *appid, *title;
  uint32_t newtags = 0;
  int i;
  const Rule *r;
  Monitor *mon = selmon, *m;

  c->isfloating = client_is_float_type(c);
  appid = client_get_appid(c);
  title = client_get_title(c);

  for (r = rules; r < END(rules); r++) {
    if ((!r->title || strstr(title, r->title)) && (!r->id || strstr(appid, r->id))) {
      c->isfloating = r->isfloating;
      newtags |= r->tags;
      i = 0;
      wl_list_for_each(m, &mons, link) {
        if (r->monitor == i++)
          mon = m;
      }
    }
  }
  if (mon) {
    c->geom.x = (mon->w.width - c->geom.width) / 2 + mon->m.x;
    c->geom.y = (mon->w.height - c->geom.height) / 2 + mon->m.y;
  }
  setmon(c, mon, newtags);
}

void arrange(Monitor *m) {
  Client *c;
  struct wlr_box target;
  int should_show;

  if (!m->wlr_output->enabled)
    return;

  if (m->switch_animate) {
    wl_list_for_each(c, &clients, link) {
      if (c->mon != m || c->close_pending)
        continue;
      if (VISIBLEONTAGS(c, m, m->prevtagset) && !VISIBLEON(c, m))
        start_tag_switch_exit_animation(c, m);
    }
  }

  wl_list_for_each(c, &clients, link) {
    if (c->mon == m) {
      should_show = VISIBLEON(c, m) || client_is_switch_exiting(c);
      wlr_scene_node_set_enabled(&c->scene->node, should_show);
      client_set_suspended(c, !should_show);
    }
  }

  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));

  wl_list_for_each(c, &clients, link) {
    if (c->mon != m || c->scene->node.parent == layers[LyrFS])
      continue;

    wlr_scene_node_reparent(&c->scene->node,
                            (!m->lt[m->sellt]->arrange && c->isfloating)
                                ? layers[LyrTile]
                            : (m->lt[m->sellt]->arrange && c->isfloating)
                                ? layers[LyrFloat]
                                : c->scene->node.parent);
  }

  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);

  wl_list_for_each(c, &clients, link) {
    if (c->mon != m || !VISIBLEON(c, m) || c->close_pending)
      continue;
    if (m->lt[m->sellt]->arrange && !c->isfloating && !c->isfullscreen)
      continue;

    target = c->isfullscreen ? m->m : c->geom;
    start_layout_animation(c, m, &target);
  }
  motionnotify(0, NULL, 0, 0, 0, 0);
  checkidleinhibitor(NULL);
}

void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive) {
  LayerSurface *l;
  struct wlr_box full_area = m->m;
  struct wlr_box target;

  wl_list_for_each(l, list, link) {
    struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

    if (!layer_surface->initialized)
      continue;
    if (l->being_unmapped)
      continue;

    if (exclusive != (layer_surface->current.exclusive_zone > 0))
      continue;

    wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
    get_layer_surface_geom(l, &target);
    wlr_scene_node_set_position(&l->popups->node, target.x, target.y);
    retarget_layer_surface(l, &target);
  }
}

void arrangelayers(Monitor *m) {
  int i;
  struct wlr_box usable_area = m->m;
  LayerSurface *l;
  uint32_t layers_above_shell[] = {
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP,
  };
  if (!m->wlr_output->enabled)
    return;

  for (i = 3; i >= 0; i--)
    arrangelayer(m, &m->layers[i], &usable_area, 1);

  if (!wlr_box_equal(&usable_area, &m->w)) {
    m->w = usable_area;
    arrange(m);
  }

  for (i = 3; i >= 0; i--)
    arrangelayer(m, &m->layers[i], &usable_area, 0);

  for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
    wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
      if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
        continue;
      focusclient(NULL, 0);
      exclusive_focus = l;
      client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
      return;
    }
  }
}

void autostartexec(void) {
  const char *const *p;
  size_t i = 0;

  for (p = autostart; *p; autostart_len++, p++)
    while (*++p)
      ;

  autostart_pids = calloc(autostart_len, sizeof(pid_t));
  for (p = autostart; *p; i++, p++) {
    if ((autostart_pids[i] = fork()) == 0) {
      setsid();
      execvp(*p, (char *const *)p);
      die("newl: execvp %s:", *p);
    }
    while (*++p)
      ;
  }
}

void axisnotify(struct wl_listener *listener, void *data) {
  struct wlr_pointer_axis_event *event = data;
  wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
  wlr_seat_pointer_notify_axis(seat,
                               event->time_msec, event->orientation, event->delta,
                               event->delta_discrete, event->source, event->relative_direction);
}

void buttonpress(struct wl_listener *listener, void *data) {
  struct wlr_pointer_button_event *event = data;
  struct wlr_keyboard *keyboard;
  uint32_t mods;
  Client *c;
  const Button *b;

  wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

  switch (event->state) {
  case WL_POINTER_BUTTON_STATE_PRESSED:
    cursor_mode = CurPressed;
    selmon = xytomon(cursor->x, cursor->y);
    if (locked)
      break;

    xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
    if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
      focusclient(c, 1);

    keyboard = wlr_seat_get_keyboard(seat);
    mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    for (b = buttons; b < END(buttons); b++) {
      if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
          event->button == b->button && b->func) {
        b->func(&b->arg);
        return;
      }
    }
    break;
  case WL_POINTER_BUTTON_STATE_RELEASED:
    if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
      wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
      cursor_mode = CurNormal;
      selmon = xytomon(cursor->x, cursor->y);
      setmon(grabc, selmon, 0);
      grabc = NULL;
      return;
    }
    cursor_mode = CurNormal;
    break;
  }
  wlr_seat_pointer_notify_button(seat,
                                 event->time_msec, event->button, event->state);
}

void chvt(const Arg *arg) {
  wlr_session_change_vt(session, arg->ui);
}

void checkidleinhibitor(struct wlr_surface *exclude) {
  int inhibited = 0, unused_lx, unused_ly;
  struct wlr_idle_inhibitor_v1 *inhibitor;
  wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
    struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
    struct wlr_scene_tree *tree = surface->data;
    if (exclude != surface && (bypass_surface_visibility || (!tree || wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
      inhibited = 1;
      break;
    }
  }

  wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void cleanup(void) {
  CloseOverlay *overlay, *tmp;
  size_t i;
  cleanuplisteners();
#ifdef XWAYLAND
  wlr_xwayland_destroy(xwayland);
  xwayland = NULL;
#endif
  wl_display_destroy_clients(dpy);
  for (i = 0; i < autostart_len; i++) {
    terminatechild(autostart_pids[i], 1);
  }
  terminatechild(child_pid, 1);
  wl_list_for_each_safe(overlay, tmp, &close_overlays, link)
      destroy_close_overlay(overlay);
  wlr_xcursor_manager_destroy(cursor_mgr);

  destroykeyboardgroup(&kb_group->destroy, NULL);
  wlr_backend_destroy(backend);
  wl_display_destroy(dpy);
  wlr_scene_node_destroy(&scene->tree.node);
}

void cleanupmon(struct wl_listener *listener, void *data) {
  CloseOverlay *overlay, *overlay_tmp;
  Monitor *m = wl_container_of(listener, m, destroy);
  LayerSurface *l, *tmp;
  size_t i;

  DwlIpcOutput *ipc_output, *ipc_output_tmp;
  wl_list_for_each_safe(ipc_output, ipc_output_tmp, &m->dwl_ipc_outputs, link)
      wl_resource_destroy(ipc_output->resource);

  for (i = 0; i < LENGTH(m->layers); i++) {
    wl_list_for_each_safe(l, tmp, &m->layers[i], link)
        wlr_layer_surface_v1_destroy(l->layer_surface);
  }
  wl_list_for_each_safe(overlay, overlay_tmp, &close_overlays, link) {
    if (overlay->mon == m)
      destroy_close_overlay(overlay);
  }

  wl_list_remove(&m->destroy.link);
  wl_list_remove(&m->frame.link);
  wl_list_remove(&m->link);
  wl_list_remove(&m->request_state.link);
  if (m->lock_surface)
    destroylocksurface(&m->destroy_lock_surface, NULL);
  m->wlr_output->data = NULL;
  wlr_output_layout_remove(output_layout, m->wlr_output);
  wlr_scene_output_destroy(m->scene_output);

  closemon(m);
  free(m);
}

void cleanuplisteners(void) {
  wl_list_remove(&cursor_axis.link);
  wl_list_remove(&cursor_button.link);
  wl_list_remove(&cursor_frame.link);
  wl_list_remove(&cursor_motion.link);
  wl_list_remove(&cursor_motion_absolute.link);
  wl_list_remove(&gpu_reset.link);
  wl_list_remove(&new_idle_inhibitor.link);
  wl_list_remove(&layout_change.link);
  wl_list_remove(&new_input_device.link);
  wl_list_remove(&new_virtual_keyboard.link);
  wl_list_remove(&new_virtual_pointer.link);
  wl_list_remove(&new_pointer_constraint.link);
  wl_list_remove(&new_output.link);
  wl_list_remove(&new_xdg_toplevel.link);
  wl_list_remove(&new_xdg_decoration.link);
  wl_list_remove(&new_xdg_popup.link);
  wl_list_remove(&new_layer_surface.link);
  wl_list_remove(&output_mgr_apply.link);
  wl_list_remove(&output_mgr_test.link);
  wl_list_remove(&output_power_mgr_set_mode.link);
  wl_list_remove(&request_activate.link);
  wl_list_remove(&request_cursor.link);
  wl_list_remove(&request_set_psel.link);
  wl_list_remove(&request_set_sel.link);
  wl_list_remove(&request_set_cursor_shape.link);
  wl_list_remove(&request_start_drag.link);
  wl_list_remove(&start_drag.link);
  wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
  wl_list_remove(&new_xwayland_surface.link);
  wl_list_remove(&xwayland_ready.link);
#endif
}

void closemon(Monitor *m) {
  Client *c;
  int i = 0, nmons = wl_list_length(&mons);
  if (!nmons) {
    selmon = NULL;
  } else if (m == selmon) {
    do
      selmon = wl_container_of(mons.next, selmon, link);
    while (!selmon->wlr_output->enabled && i++ < nmons);

    if (!selmon->wlr_output->enabled)
      selmon = NULL;
  }

  wl_list_for_each(c, &clients, link) {
    if (c->isfloating && c->geom.x > m->m.width)
      client_request_geometry(c,
                              &(struct wlr_box){
                                  .x = c->geom.x - m->w.width,
                                  .y = c->geom.y,
                                  .width = c->geom.width,
                                  .height = c->geom.height,
                              },
                              0);
    if (c->mon == m)
      setmon(c, selmon, c->tags);
  }
  focusclient(focustop(selmon), 1);
  printstatus();
}

void commitlayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *l = wl_container_of(listener, l, surface_commit);
  struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
  struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
  float opacity;
  struct wlr_layer_surface_v1_state old_state;

  if (l->layer_surface->initial_commit) {
    client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);
    old_state = l->layer_surface->current;
    l->layer_surface->current = l->layer_surface->pending;
    arrangelayers(l->mon);
    l->layer_surface->current = old_state;
    return;
  }

  if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
    return;
  l->mapped = layer_surface->surface->mapped;

  if (scene_layer != l->scene->node.parent) {
    wlr_scene_node_reparent(&l->scene->node, scene_layer);
    wl_list_remove(&l->link);
    wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
    wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
  }

  arrangelayers(l->mon);
  sample_animation_state(&l->anim, NULL, NULL, &opacity, NULL, NULL);
  apply_layer_surface_opacity(l, opacity);
}

void commitnotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, commit);
  struct wlr_box size;

#ifdef XWAYLAND
  if (client_is_x11(c)) {
    client_handle_x11_commit(c);
    return;
  }
#endif

  if (c->surface.xdg->initial_commit) {
    applyrules(c);
    if (c->mon) {
      client_set_scale(client_surface(c), c->mon->wlr_output->scale);
    }
    if (client_has_initial_position(c, c->mon))
      c->opacity = 0.0f;
    setmon(c, NULL, 0);
    wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
                                         WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
    if (c->decoration)
      requestdecorationmode(&c->set_decoration_mode, c->decoration);
  }

  if (c->scene) {
    if (client_get_xdg_committed_size(c, &size)) {
      client_apply_committed_geometry(c,
                                      size.width + 2 * c->bw,
                                      size.height + 2 * c->bw);
    }

    if (c->pending_configure_serial && c->pending_configure_serial == c->surface.xdg->current.configure_serial)
      client_clear_pending_configure(c);

    if (!c->pending_configure_serial && !c->anim.active) {
      if (!client_should_use_visual_geometry(c, &c->current_geom, &c->geom))
        c->geom = c->current_geom;
      client_set_scheduled_size(
          c, client_should_use_visual_geometry(c, &c->current_geom, &c->geom)
                 ? &c->geom
                 : &c->current_geom);
    }
  }

  client_apply_sampled_opacity(c);
}

void commitpopup(struct wl_listener *listener, void *data) {
  struct wlr_surface *surface = data;
  struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
  LayerSurface *l = NULL;
  Client *c = NULL;
  struct wlr_box box;
  int type = -1;

  if (!popup->base->initial_commit)
    return;

  type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
  if (!popup->parent || type < 0)
    return;
  popup->base->surface->data = wlr_scene_xdg_surface_create(
      popup->parent->data, popup->base);
  if ((l && !l->mon) || (c && !c->mon)) {
    wlr_xdg_popup_destroy(popup);
    return;
  }
  box = type == LayerShell ? l->mon->m : c->mon->w;
  box.x -= type == LayerShell ? l->scene->node.x : c->current_geom.x;
  box.y -= type == LayerShell ? l->scene->node.y : c->current_geom.y;
  wlr_xdg_popup_unconstrain_from_box(popup, &box);
  wl_list_remove(&listener->link);
  free(listener);
}

void createdecoration(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel_decoration_v1 *deco = data;
  Client *c = deco->toplevel->base->data;
  c->decoration = deco;

  LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
  LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

  requestdecorationmode(&c->set_decoration_mode, deco);
}

void createidleinhibitor(struct wl_listener *listener, void *data) {
  struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
  LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

  checkidleinhibitor(NULL);
}

void createkeyboard(struct wlr_keyboard *keyboard) {
  wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);
  wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void) {
  KeyboardGroup *group = ecalloc(1, sizeof(*group));
  struct xkb_context *context;
  struct xkb_keymap *keymap;

  group->wlr_group = wlr_keyboard_group_create();
  group->wlr_group->data = group;
  context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS)))
    die("failed to compile keymap");

  wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);

  wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

  LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
  LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

  group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

  wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
  return group;
}

void createlayersurface(struct wl_listener *listener, void *data) {
  struct wlr_layer_surface_v1 *layer_surface = data;
  LayerSurface *l;
  struct wlr_surface *surface = layer_surface->surface;
  struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

  if (!layer_surface->output && !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
    wlr_layer_surface_v1_destroy(layer_surface);
    return;
  }

  l = layer_surface->data = ecalloc(1, sizeof(*l));
  l->type = LayerShell;
  l->opacity = 1.0f;
  l->initial_position = 1;
  LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
  LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
  LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

  l->layer_surface = layer_surface;
  l->mon = layer_surface->output->data;
  l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
  l->scene = l->scene_layer->tree;
  l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
  l->scene->node.data = l->popups->node.data = l;

  wl_list_insert(&l->mon->layers[layer_surface->pending.layer], &l->link);
  wlr_surface_send_enter(surface, layer_surface->output);
}

void createlocksurface(struct wl_listener *listener, void *data) {
  SessionLock *lock = wl_container_of(listener, lock, new_surface);
  struct wlr_session_lock_surface_v1 *lock_surface = data;
  Monitor *m = lock_surface->output->data;
  struct wlr_scene_tree *scene_tree = lock_surface->surface->data = wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
  m->lock_surface = lock_surface;

  wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
  wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

  LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

  if (m == selmon)
    client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void createmon(struct wl_listener *listener, void *data) {
  struct wlr_output *wlr_output = data;
  const MonitorRule *r;
  size_t i;
  struct wlr_output_state state;
  Monitor *m;

  if (!wlr_output_init_render(wlr_output, alloc, drw))
    return;

  m = wlr_output->data = ecalloc(1, sizeof(*m));
  m->wlr_output = wlr_output;

  wl_list_init(&m->dwl_ipc_outputs);

  for (i = 0; i < LENGTH(m->layers); i++)
    wl_list_init(&m->layers[i]);

  m->gaps = gaps;

  wlr_output_state_init(&state);
  m->tagset[0] = m->tagset[1] = 1;
  for (r = monrules; r < END(monrules); r++) {
    if (!r->name || strstr(wlr_output->name, r->name)) {
      m->m.x = r->x;
      m->m.y = r->y;
      m->mfact = r->mfact;
      m->lt[0] = r->lt;
      m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
      strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));
      wlr_output_state_set_scale(&state, r->scale);
      wlr_output_state_set_transform(&state, r->rr);
      break;
    }
  }
  wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

  LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
  LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
  LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

  wlr_output_state_set_enabled(&state, 1);
  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state);

  wl_list_insert(&mons, &m->link);
  printstatus();

  m->scene_output = wlr_scene_output_create(scene, wlr_output);
  if (m->m.x == -1 && m->m.y == -1)
    wlr_output_layout_add_auto(output_layout, wlr_output);
  else
    wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void createnotify(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel *toplevel = data;
  Client *c = NULL;

  c = toplevel->base->data = ecalloc(1, sizeof(*c));
  c->surface.xdg = toplevel->base;
  client_init_common(c, XDGShell, borderpx, 1);

  LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
  LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
  LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
  LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
  LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
  LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
  LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void createpointer(struct wlr_pointer *pointer) {
  struct libinput_device *device;
  if (wlr_input_device_is_libinput(&pointer->base) && (device = wlr_libinput_get_device_handle(&pointer->base))) {

    if (libinput_device_config_tap_get_finger_count(device)) {
      libinput_device_config_tap_set_enabled(device, tap_to_click);
      libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
      libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
      libinput_device_config_tap_set_button_map(device, button_map);
    }

    if (libinput_device_config_scroll_has_natural_scroll(device))
      libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

    if (libinput_device_config_dwt_is_available(device))
      libinput_device_config_dwt_set_enabled(device, disable_while_typing);

    if (libinput_device_config_left_handed_is_available(device))
      libinput_device_config_left_handed_set(device, left_handed);

    if (libinput_device_config_middle_emulation_is_available(device))
      libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

    if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
      libinput_device_config_scroll_set_method(device, scroll_method);

    if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
      libinput_device_config_click_set_method(device, click_method);

    if (libinput_device_config_send_events_get_modes(device))
      libinput_device_config_send_events_set_mode(device, send_events_mode);

    if (libinput_device_config_accel_is_available(device)) {
      libinput_device_config_accel_set_profile(device, accel_profile);
      libinput_device_config_accel_set_speed(device, accel_speed);
    }
  }

  wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void createpointerconstraint(struct wl_listener *listener, void *data) {
  PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
  pointer_constraint->constraint = data;
  LISTEN(&pointer_constraint->constraint->events.destroy,
         &pointer_constraint->destroy, destroypointerconstraint);
}

void createpopup(struct wl_listener *listener, void *data) {
  struct wlr_xdg_popup *popup = data;
  LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint) {
  if (active_constraint == constraint)
    return;

  if (active_constraint)
    wlr_pointer_constraint_v1_send_deactivated(active_constraint);

  active_constraint = constraint;
  wlr_pointer_constraint_v1_send_activated(constraint);
}

void cursorframe(struct wl_listener *listener, void *data) {
  wlr_seat_pointer_notify_frame(seat);
}

void cursorwarptohint(void) {
  Client *c = NULL;
  double sx = active_constraint->current.cursor_hint.x;
  double sy = active_constraint->current.cursor_hint.y;

  toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
  if (c && active_constraint->current.cursor_hint.enabled) {
    wlr_cursor_warp(cursor, NULL,
                    sx + c->current_geom.x + c->bw,
                    sy + c->current_geom.y + c->bw);
    wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
  }
}

void destroydecoration(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, destroy_decoration);

  wl_list_remove(&c->destroy_decoration.link);
  wl_list_remove(&c->set_decoration_mode.link);
}

void destroydragicon(struct wl_listener *listener, void *data) {
  focusclient(focustop(selmon), 1);
  motionnotify(0, NULL, 0, 0, 0, 0);
  wl_list_remove(&listener->link);
  free(listener);
}

void destroyidleinhibitor(struct wl_listener *listener, void *data) {
  checkidleinhibitor(wlr_surface_get_root_surface(data));
  wl_list_remove(&listener->link);
  free(listener);
}

void destroylayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *l = wl_container_of(listener, l, destroy);
  wl_list_remove(&l->link);
  wl_list_remove(&l->destroy.link);
  wl_list_remove(&l->unmap.link);
  wl_list_remove(&l->surface_commit.link);
  wlr_scene_node_destroy(&l->scene->node);
  wlr_scene_node_destroy(&l->popups->node);
  free(l);
}

void destroylock(SessionLock *lock, int unlock) {
  wlr_seat_keyboard_notify_clear_focus(seat);
  if ((locked = !unlock))
    goto destroy;

  wlr_scene_node_set_enabled(&locked_bg->node, 0);

  focusclient(focustop(selmon), 0);
  motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
  wl_list_remove(&lock->new_surface.link);
  wl_list_remove(&lock->unlock.link);
  wl_list_remove(&lock->destroy.link);

  wlr_scene_node_destroy(&lock->scene->node);
  cur_lock = NULL;
  free(lock);
}

void destroylocksurface(struct wl_listener *listener, void *data) {
  Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
  struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

  m->lock_surface = NULL;
  wl_list_remove(&m->destroy_lock_surface.link);

  if (lock_surface->surface != seat->keyboard_state.focused_surface)
    return;

  if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
    surface = wl_container_of(cur_lock->surfaces.next, surface, link);
    client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
  } else if (!locked) {
    focusclient(focustop(selmon), 1);
  } else {
    wlr_seat_keyboard_clear_focus(seat);
  }
}

void destroynotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, destroy);
  if (c == focus_anchor)
    focus_anchor = NULL;
  client_clear_pending_configure(c);
  wl_list_remove(&c->destroy.link);
  wl_list_remove(&c->set_title.link);
  wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
  if (c->type != XDGShell) {
    if (!wl_list_empty(&c->commit.link)) {
      wl_list_remove(&c->commit.link);
      wl_list_init(&c->commit.link);
    }
    wl_list_remove(&c->activate.link);
    wl_list_remove(&c->associate.link);
    wl_list_remove(&c->configure.link);
    wl_list_remove(&c->dissociate.link);
    wl_list_remove(&c->set_hints.link);
  } else
#endif
  {
    wl_list_remove(&c->commit.link);
    wl_list_init(&c->commit.link);
    wl_list_remove(&c->map.link);
    wl_list_init(&c->map.link);
    wl_list_remove(&c->unmap.link);
    wl_list_init(&c->unmap.link);
    wl_list_remove(&c->maximize.link);
  }
  free(c);
}

void destroypointerconstraint(struct wl_listener *listener, void *data) {
  PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

  if (active_constraint == pointer_constraint->constraint) {
    cursorwarptohint();
    active_constraint = NULL;
  }

  wl_list_remove(&pointer_constraint->destroy.link);
  free(pointer_constraint);
}

void destroysessionlock(struct wl_listener *listener, void *data) {
  SessionLock *lock = wl_container_of(listener, lock, destroy);
  destroylock(lock, 0);
}

void destroykeyboardgroup(struct wl_listener *listener, void *data) {
  KeyboardGroup *group = wl_container_of(listener, group, destroy);
  wl_event_source_remove(group->key_repeat_source);
  wl_list_remove(&group->key.link);
  wl_list_remove(&group->modifiers.link);
  wl_list_remove(&group->destroy.link);
  wlr_keyboard_group_destroy(group->wlr_group);
  free(group);
}

Monitor *
dirtomon(enum wlr_direction dir) {
  struct wlr_output *next;
  if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
    return selmon;
  if ((next = wlr_output_layout_adjacent_output(output_layout,
                                                dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
    return next->data;
  if ((next = wlr_output_layout_farthest_output(output_layout,
                                                dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
                                                selmon->wlr_output, selmon->m.x, selmon->m.y)))
    return next->data;
  return selmon;
}

void focusclient(Client *c, int lift) {
  struct wlr_surface *old = seat->keyboard_state.focused_surface;
  int unused_lx, unused_ly, old_client_type;
  Client *old_c = NULL;
  LayerSurface *old_l = NULL;

  if (locked || (c && client_surface(c) == old))
    return;

  if (c && lift)
    wlr_scene_node_raise_to_top(&c->scene->node);

  if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
    struct wlr_xdg_popup *popup, *tmp;
    wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
        wlr_xdg_popup_destroy(popup);
  }

  if (c && !client_is_unmanaged(c)) {
    focus_anchor = c;
    selmon = c->mon;
    c->isurgent = 0;

    if (!exclusive_focus && !seat->drag)
      client_set_border_color(c, focuscolor);
  }

  if (old && (!c || client_surface(c) != old)) {
    if (old_client_type == LayerShell && wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) && old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
      return;
    } else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
      return;
    } else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
      client_set_border_color(old_c, bordercolor);

      client_activate_surface(old, 0);
    }
  }
  printstatus();

  if (!c) {
    wlr_seat_keyboard_notify_clear_focus(seat);
    return;
  }
  motionnotify(0, NULL, 0, 0, 0, 0);
  client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));
  client_activate_surface(client_surface(c), 1);
}

void focusmon(const Arg *arg) {
  int i = 0, nmons = wl_list_length(&mons);
  if (nmons) {
    selmon = dirtomon(arg->i);
    while (!selmon->wlr_output->enabled && i++ < nmons)
      ;
  }
  focusclient(focustop(selmon), 1);
}

void focusstack(const Arg *arg) {
  Client *c, *sel = focustop(selmon);
  if (!sel || (sel->isfullscreen && !client_has_children(sel)))
    return;
  if (arg->i > 0) {
    wl_list_for_each(c, &sel->link, link) {
      if (&c->link == &clients)
        continue;
      if (VISIBLEON(c, selmon))
        break;
    }
  } else {
    wl_list_for_each_reverse(c, &sel->link, link) {
      if (&c->link == &clients)
        continue;
      if (VISIBLEON(c, selmon))
        break;
    }
  }
  focusclient(c, 1);
}

Client *
focustop(Monitor *m) {
  Client *c;
  if (focus_anchor) {
    if (VISIBLEON(focus_anchor, m))
      return focus_anchor;
    if ((c = focus_fallback_from(focus_anchor, m)))
      return c;
  }
  wl_list_for_each_reverse(c, &clients, link) {
    if (VISIBLEON(c, m))
      return c;
  }
  return NULL;
}

void fullscreennotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, fullscreen);
  setfullscreen(c, client_wants_fullscreen(c));
}

void gpureset(struct wl_listener *listener, void *data) {
  struct wlr_renderer *old_drw = drw;
  struct wlr_allocator *old_alloc = alloc;
  struct Monitor *m;
  if (!(drw = wlr_renderer_autocreate(backend)))
    die("couldn't recreate renderer");

  if (!(alloc = wlr_allocator_autocreate(backend, drw)))
    die("couldn't recreate allocator");

  wl_list_remove(&gpu_reset.link);
  wl_signal_add(&drw->events.lost, &gpu_reset);

  wlr_compositor_set_renderer(compositor, drw);

  wl_list_for_each(m, &mons, link) {
    wlr_output_init_render(m->wlr_output, alloc, drw);
  }

  wlr_allocator_destroy(old_alloc);
  wlr_renderer_destroy(old_drw);
}

void handlesig(int signo) {
  if (signo == SIGCHLD)
    while (waitpid(-1, NULL, WNOHANG) > 0)
      ;
  else if (signo == SIGINT || signo == SIGTERM)
    quit(NULL);
}

void inputdevice(struct wl_listener *listener, void *data) {
  struct wlr_input_device *device = data;
  uint32_t caps;

  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    createkeyboard(wlr_keyboard_from_input_device(device));
    break;
  case WLR_INPUT_DEVICE_POINTER:
    createpointer(wlr_pointer_from_input_device(device));
    break;
  default:
    break;
  }

  caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&kb_group->wlr_group->devices))
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  wlr_seat_set_capabilities(seat, caps);
}

int keybinding(uint32_t mods, xkb_keysym_t sym) {
  const Key *k;
  for (k = keys; k < END(keys); k++) {
    if (CLEANMASK(mods) == CLEANMASK(k->mod) && sym == k->keysym && k->func) {
      k->func(&k->arg);
      return 1;
    }
  }
  return 0;
}

void keypress(struct wl_listener *listener, void *data) {
  int i;
  KeyboardGroup *group = wl_container_of(listener, group, key);
  struct wlr_keyboard_key_event *event = data;

  uint32_t keycode = event->keycode + 8;
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms(
      group->wlr_group->keyboard.xkb_state, keycode, &syms);

  int handled = 0;
  uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

  wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

  if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    for (i = 0; i < nsyms; i++)
      handled = keybinding(mods, syms[i]) || handled;
  }

  if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
    group->mods = mods;
    group->keysyms = syms;
    group->nsyms = nsyms;
    wl_event_source_timer_update(group->key_repeat_source,
                                 group->wlr_group->keyboard.repeat_info.delay);
  } else {
    group->nsyms = 0;
    wl_event_source_timer_update(group->key_repeat_source, 0);
  }

  if (handled)
    return;

  wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
  wlr_seat_keyboard_notify_key(seat, event->time_msec,
                               event->keycode, event->state);
}

void keypressmod(struct wl_listener *listener, void *data) {
  KeyboardGroup *group = wl_container_of(listener, group, modifiers);

  wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
  wlr_seat_keyboard_notify_modifiers(seat,
                                     &group->wlr_group->keyboard.modifiers);
}

int keyrepeat(void *data) {
  KeyboardGroup *group = data;
  int i;
  if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
    return 0;

  wl_event_source_timer_update(group->key_repeat_source,
                               1000 / group->wlr_group->keyboard.repeat_info.rate);

  for (i = 0; i < group->nsyms; i++)
    keybinding(group->mods, group->keysyms[i]);

  return 0;
}

void killclient(const Arg *arg) {
  Client *sel = focustop(selmon);
  if (sel) {
    start_close_animation(sel);
    arrange(selmon);
    focusclient(focustop(selmon), 1);
    printstatus();
  }
}

void locksession(struct wl_listener *listener, void *data) {
  struct wlr_session_lock_v1 *session_lock = data;
  SessionLock *lock;
  wlr_scene_node_set_enabled(&locked_bg->node, 1);
  if (cur_lock) {
    wlr_session_lock_v1_destroy(session_lock);
    return;
  }
  lock = session_lock->data = ecalloc(1, sizeof(*lock));
  focusclient(NULL, 0);

  lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
  cur_lock = lock->lock = session_lock;
  locked = 1;

  LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
  LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
  LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

  wlr_session_lock_v1_send_locked(session_lock);
}

void mapnotify(struct wl_listener *listener, void *data) {
  Client *p = NULL;
  Client *w, *c = wl_container_of(listener, c, map);
  Monitor *animmon, *m;

  p = client_get_parent(c);
  animmon = p && p->mon ? p->mon : xytomon(c->geom.x, c->geom.y);
  if (client_has_initial_position(c, animmon))
    c->opacity = 0.0f;

  client_create_scene(c);
  client_bootstrap_geometry(c);

  if (client_map_unmanaged(c))
    goto unset_fullscreen;

  client_finish_managed_map(c, p);
  printstatus();

unset_fullscreen:
  m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
  wl_list_for_each(w, &clients, link) {
    if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
      setfullscreen(w, 0);
  }
}

void maximizenotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, maximize);
  if (c->surface.xdg->initialized)
    wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void monocle(Monitor *m) {
  Client *c;
  int n = 0;

  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
      continue;
    n++;
    start_layout_animation(c, m, &m->w);
  }
  if (n)
    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
  if ((c = focustop(m)))
    wlr_scene_node_raise_to_top(&c->scene->node);
}

void motionabsolute(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_absolute_event *event = data;
  double lx, ly, dx, dy;

  if (!event->time_msec)
    wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

  wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
  dx = lx - cursor->x;
  dy = ly - cursor->y;
  motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
                  double dx_unaccel, double dy_unaccel) {
  double sx = 0, sy = 0, sx_confined, sy_confined;
  Client *c = NULL, *w = NULL;
  LayerSurface *l = NULL;
  struct wlr_surface *surface = NULL;
  struct wlr_pointer_constraint_v1 *constraint;

  xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

  if (cursor_mode == CurPressed && !seat->drag && surface != seat->pointer_state.focused_surface && toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
    c = w;
    surface = seat->pointer_state.focused_surface;
    sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
    sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
  }

  if (time) {
    wlr_relative_pointer_manager_v1_send_relative_motion(
        relative_pointer_mgr, seat, (uint64_t)time * 1000,
        dx, dy, dx_unaccel, dy_unaccel);

    wl_list_for_each(constraint, &pointer_constraints->constraints, link)
        cursorconstrain(constraint);

    if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
      toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
      if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
        sx = cursor->x - c->current_geom.x - c->bw;
        sy = cursor->y - c->current_geom.y - c->bw;
        if (wlr_region_confine(&active_constraint->region, sx, sy,
                               sx + dx, sy + dy, &sx_confined, &sy_confined)) {
          dx = sx_confined - sx;
          dy = sy_confined - sy;
        }

        if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
          return;
      }
    }

    wlr_cursor_move(cursor, device, dx, dy);
    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    if (sloppyfocus)
      selmon = xytomon(cursor->x, cursor->y);
  }

  wlr_scene_node_set_position(&drag_icon->node, (int)(cursor->x), (int)(cursor->y));

  if (cursor_mode == CurMove) {
    client_request_geometry(grabc,
                            &(struct wlr_box){
                                .x = (int)(cursor->x) - grabcx,
                                .y = (int)(cursor->y) - grabcy,
                                .width = grabc->current_geom.width,
                                .height = grabc->current_geom.height,
                            },
                            1);
    return;
  } else if (cursor_mode == CurResize) {
    client_request_geometry(grabc,
                            &(struct wlr_box){
                                .x = grabc->current_geom.x,
                                .y = grabc->current_geom.y,
                                .width = (int)(cursor->x) - grabc->current_geom.x,
                                .height = (int)(cursor->y) - grabc->current_geom.y,
                            },
                            1);
    return;
  }

  if (!surface && !seat->drag)
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

  pointerfocus(c, surface, sx, sy, time);
}

void motionrelative(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_event *event = data;
  motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
               event->unaccel_dx, event->unaccel_dy);
}

void moveresize(const Arg *arg) {
  if (cursor_mode != CurNormal && cursor_mode != CurPressed)
    return;
  xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
  if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
    return;
  setfloating(grabc, 1);
  switch (cursor_mode = arg->ui) {
  case CurMove:
    grabcx = (int)(cursor->x) - grabc->current_geom.x;
    grabcy = (int)(cursor->y) - grabc->current_geom.y;
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "fleur");
    break;
  case CurResize:
    wlr_cursor_warp_closest(cursor, NULL,
                            grabc->current_geom.x + grabc->current_geom.width,
                            grabc->current_geom.y + grabc->current_geom.height);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
    break;
  }
}

void outputmgrapply(struct wl_listener *listener, void *data) {
  struct wlr_output_configuration_v1 *config = data;
  outputmgrapplyortest(config, 0);
}

void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test) {
  struct wlr_output_configuration_head_v1 *config_head;
  int ok = 1;

  wl_list_for_each(config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;
    Monitor *m = wlr_output->data;
    struct wlr_output_state state;
    m->asleep = 0;

    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, config_head->state.enabled);
    if (!config_head->state.enabled)
      goto apply_or_test;

    if (config_head->state.mode)
      wlr_output_state_set_mode(&state, config_head->state.mode);
    else
      wlr_output_state_set_custom_mode(&state,
                                       config_head->state.custom_mode.width,
                                       config_head->state.custom_mode.height,
                                       config_head->state.custom_mode.refresh);

    wlr_output_state_set_transform(&state, config_head->state.transform);
    wlr_output_state_set_scale(&state, config_head->state.scale);
    wlr_output_state_set_adaptive_sync_enabled(&state,
                                               config_head->state.adaptive_sync_enabled);

  apply_or_test:
    ok &= test ? wlr_output_test_state(wlr_output, &state)
               : wlr_output_commit_state(wlr_output, &state);
    if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
      wlr_output_layout_add(output_layout, wlr_output,
                            config_head->state.x, config_head->state.y);
    wlr_output_state_finish(&state);
  }
  if (ok)
    wlr_output_configuration_v1_send_succeeded(config);
  else
    wlr_output_configuration_v1_send_failed(config);
  wlr_output_configuration_v1_destroy(config);

  updatemons(NULL, NULL);
}

void outputmgrtest(struct wl_listener *listener, void *data) {
  struct wlr_output_configuration_v1 *config = data;
  outputmgrapplyortest(config, 1);
}

void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
                  uint32_t time) {
  struct timespec now;

  if (surface != seat->pointer_state.focused_surface &&
      sloppyfocus && time && c && !client_is_unmanaged(c))
    focusclient(c, 0);

  if (!surface) {
    wlr_seat_pointer_notify_clear_focus(seat);
    return;
  }

  if (!time) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
  }
  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void printstatus(void) {
  Monitor *m = NULL;
  wl_list_for_each(m, &mons, link)
      dwl_ipc_output_printstatus(m);
}

void powermgrsetmode(struct wl_listener *listener, void *data) {
  struct wlr_output_power_v1_set_mode_event *event = data;
  struct wlr_output_state state = {0};
  Monitor *m = event->output->data;

  if (!m)
    return;

  m->gamma_lut_changed = 1;
  wlr_output_state_set_enabled(&state, event->mode);
  wlr_output_commit_state(m->wlr_output, &state);

  m->asleep = !event->mode;
  updatemons(NULL, NULL);
}

void quit(const Arg *arg) {
  wl_display_terminate(dpy);
}

void rendermon(struct wl_listener *listener, void *data) {
  CloseOverlay *overlay, *tmp;
  LayerSurface *l;
  Monitor *m = wl_container_of(listener, m, frame);
  Client *c;
  struct wlr_output_state pending = {0};
  struct timespec now;
  int i;
  int needs_frame = 0;

  clock_gettime(CLOCK_MONOTONIC, &now);
  wl_list_for_each(c, &clients, link) {
    if (c->mon != m || !c->anim.active)
      continue;
    needs_frame |= step_client_animation_frame(c, &now);
  }
  for (i = 0; i < 4; i++) {
    wl_list_for_each(l, &m->layers[i], link) {
      if (!l->mapped || !l->anim.active)
        continue;
      needs_frame |= step_layer_surface_animation_frame(l, &now);
    }
  }
  wl_list_for_each_safe(overlay, tmp, &close_overlays, link) {
    if (overlay->mon != m)
      continue;
    needs_frame |= step_close_overlay_frame(overlay, &now);
  }
  if (needs_frame)
    wlr_output_schedule_frame(m->wlr_output);
  wlr_scene_output_commit(m->scene_output, NULL);
  wlr_scene_output_send_frame_done(m->scene_output, &now);
  wlr_output_state_finish(&pending);
}

void requestdecorationmode(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, set_decoration_mode);
  if (c->surface.xdg->initialized)
    wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void requeststartdrag(struct wl_listener *listener, void *data) {
  struct wlr_seat_request_start_drag_event *event = data;
  if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
                                            event->serial))
    wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
  else
    wlr_data_source_destroy(event->drag->source);
}

void requestmonstate(struct wl_listener *listener, void *data) {
  struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(event->output, event->state);
  updatemons(NULL, NULL);
}

void run(char *startup_cmd) {
  const char *socket = wl_display_add_socket_auto(dpy);
  if (!socket)
    die("startup: display_add_socket_auto");
  setenv("WAYLAND_DISPLAY", socket, 1);
  if (!wlr_backend_start(backend))
    die("startup: backend_start");
  autostartexec();
  if (startup_cmd) {
    int piperw[2];
    if (pipe(piperw) < 0)
      die("startup: pipe:");
    if ((child_pid = fork()) < 0)
      die("startup: fork:");
    if (child_pid == 0) {
      setsid();
      dup2(piperw[0], STDIN_FILENO);
      close(piperw[0]);
      close(piperw[1]);
      execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
      die("startup: execl:");
    }
    dup2(piperw[1], STDOUT_FILENO);
    close(piperw[1]);
    close(piperw[0]);
  }

  if (fd_set_nonblock(STDOUT_FILENO) < 0)
    close(STDOUT_FILENO);

  printstatus();

  selmon = xytomon(cursor->x, cursor->y);
  wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
  wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
  wl_display_run(dpy);
}

void setcursor(struct wl_listener *listener, void *data) {
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  if (cursor_mode != CurNormal && cursor_mode != CurPressed)
    return;
  if (event->seat_client == seat->pointer_state.focused_client)
    wlr_cursor_set_surface(cursor, event->surface,
                           event->hotspot_x, event->hotspot_y);
}

void setcursorshape(struct wl_listener *listener, void *data) {
  struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
  if (cursor_mode != CurNormal && cursor_mode != CurPressed)
    return;
  if (event->seat_client == seat->pointer_state.focused_client)
    wlr_cursor_set_xcursor(cursor, cursor_mgr,
                           wlr_cursor_shape_v1_name(event->shape));
}

void setfloating(Client *c, int floating) {
  Client *p = client_get_parent(c);
  c->isfloating = floating;
  if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
    return;
  wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
                                                          (p && p->isfullscreen)
                                                      ? LyrFS
                                                  : c->isfloating ? LyrFloat
                                                                  : LyrTile]);
  arrange(c->mon);
  printstatus();
}

void setfullscreen(Client *c, int fullscreen) {
  c->isfullscreen = fullscreen;
  if (!c->mon || !client_surface(c)->mapped)
    return;
  c->bw = fullscreen ? 0 : borderpx;
  client_set_fullscreen(c, fullscreen);
  wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
                                                      ? LyrFS
                                                  : c->isfloating ? LyrFloat
                                                                  : LyrTile]);

  if (fullscreen) {
    c->prev = c->geom;
    start_animation_at(c, &c->mon->m, NULL, 1.0f, NULL);
  } else {
    start_animation_at(c, &c->prev, NULL, 1.0f, NULL);
  }
  arrange(c->mon);
  printstatus();
}

void setlayout(const Arg *arg) {
  if (!selmon)
    return;
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
    selmon->sellt ^= 1;
  if (arg && arg->v)
    selmon->lt[selmon->sellt] = (Layout *)arg->v;
  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, LENGTH(selmon->ltsymbol));
  arrange(selmon);
  printstatus();
}

void setmonitortags(Monitor *m, uint32_t newtags, int toggle_tagset) {
  uint32_t added_tags, prev_tags;
  int added_index, prev_index;

  if (!m || !newtags || newtags == m->tagset[m->seltags])
    return;

  prev_tags = m->tagset[m->seltags];
  clock_gettime(CLOCK_MONOTONIC, &m->switch_start_time);
  m->switch_offset = get_monitor_switch_offset(m, prev_tags);
  cancel_tag_switch_exit_animations(m);
  if (toggle_tagset)
    m->seltags ^= 1;
  m->tagset[m->seltags] = newtags;
  m->prevtagset = prev_tags;

  prev_index = prev_tags ? __builtin_ffs(prev_tags) - 1 : -1;
  added_tags = newtags & ~prev_tags;
  added_index = added_tags ? __builtin_ffs(added_tags) - 1 : -1;

  m->switch_animate = 1;
  m->switch_dir = (added_index >= 0 ? added_index : __builtin_ffs(newtags) - 1) > prev_index ? 1 : -1;

  if (selmon == m)
    focusclient(focustop(m), 1);
  arrange(m);
  printstatus();

  m->switch_animate = 0;
  m->switch_offset = 0;
}

void setmfact(const Arg *arg) {
  float f;

  if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
  if (f < 0.1 || f > 0.9)
    return;
  selmon->mfact = f;
  arrange(selmon);
}

void setmon(Client *c, Monitor *m, uint32_t newtags) {
  Monitor *oldmon = c->mon;

  if (oldmon == m)
    return;
  c->mon = m;
  c->prev = c->geom;

  if (oldmon)
    arrange(oldmon);
  if (m) {
    client_request_geometry(c, &c->geom, 0);
    c->tags = newtags ? newtags : m->tagset[m->seltags];
    setfullscreen(c, c->isfullscreen);
    setfloating(c, c->isfloating);
  }
  focusclient(focustop(selmon), 1);
}

void setpsel(struct wl_listener *listener, void *data) {
  struct wlr_seat_request_set_primary_selection_event *event = data;
  wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void setsel(struct wl_listener *listener, void *data) {
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(seat, event->source, event->serial);
}

void setup(void) {
  int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
  struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
  sigemptyset(&sa.sa_mask);
  init_bezier();

  for (i = 0; i < (int)LENGTH(sig); i++)
    sigaction(sig[i], &sa, NULL);

  wlr_log_init(log_level, NULL);
  dpy = wl_display_create();
  event_loop = wl_display_get_event_loop(dpy);
  wl_display_set_default_max_buffer_size(dpy, 1024 * 1024);

  if (!(backend = wlr_backend_autocreate(event_loop, &session)))
    die("couldn't create backend");
  scene = wlr_scene_create();
  for (i = 0; i < NUM_LAYERS; i++)
    layers[i] = wlr_scene_tree_create(&scene->tree);
  drag_icon = wlr_scene_tree_create(&scene->tree);
  close_overlay_tree = wlr_scene_tree_create(&scene->tree);
  wlr_scene_node_place_below(&close_overlay_tree->node, &layers[LyrBlock]->node);
  wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);
  wl_list_init(&close_overlays);

  if (!(drw = wlr_renderer_autocreate(backend)))
    die("couldn't create renderer");
  wl_signal_add(&drw->events.lost, &gpu_reset);
  wlr_renderer_init_wl_shm(drw, dpy);

  if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
    wlr_drm_create(dpy, drw);
    wlr_scene_set_linux_dmabuf_v1(scene,
                                  wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
  }
  if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline && backend->features.timeline)
    wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);
  if (!(alloc = wlr_allocator_autocreate(backend, drw)))
    die("couldn't create allocator");

  compositor = wlr_compositor_create(dpy, 6, drw);
  wlr_subcompositor_create(dpy);
  wlr_data_device_manager_create(dpy);
  wlr_export_dmabuf_manager_v1_create(dpy);
  wlr_ext_image_copy_capture_manager_v1_create(dpy, 1);
  wlr_ext_output_image_capture_source_manager_v1_create(dpy, 1);
  wlr_data_control_manager_v1_create(dpy);
  wlr_primary_selection_v1_device_manager_create(dpy);
  wlr_viewporter_create(dpy);
  wlr_single_pixel_buffer_manager_v1_create(dpy);
  wlr_fractional_scale_manager_v1_create(dpy, 1);
  wlr_presentation_create(dpy, backend, 2);
  wlr_alpha_modifier_v1_create(dpy);

  activation = wlr_xdg_activation_v1_create(dpy);
  wl_signal_add(&activation->events.request_activate, &request_activate);

  wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

  power_mgr = wlr_output_power_manager_v1_create(dpy);
  wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

  output_layout = wlr_output_layout_create(dpy);
  wl_signal_add(&output_layout->events.change, &layout_change);
  wlr_xdg_output_manager_v1_create(dpy, output_layout);

  wl_list_init(&mons);
  wl_signal_add(&backend->events.new_output, &new_output);

  wl_list_init(&clients);

  xdg_shell = wlr_xdg_shell_create(dpy, 6);
  wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
  wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

  layer_shell = wlr_layer_shell_v1_create(dpy, 3);
  wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

  idle_notifier = wlr_idle_notifier_v1_create(dpy);

  idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
  wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

  session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
  wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);

  locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
                                    (float[4]){0.1f, 0.1f, 0.1f, 1.0f});
  wlr_scene_node_set_enabled(&locked_bg->node, 0);

  wlr_server_decoration_manager_set_default_mode(
      wlr_server_decoration_manager_create(dpy),
      WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
  xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
  wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

  pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
  wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

  relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

  cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor, output_layout);

  cursor_mgr = wlr_xcursor_manager_create("NULL", 24);
  setenv("XCURSOR_SIZE", "24", 1);

  wl_signal_add(&cursor->events.motion, &cursor_motion);
  wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
  wl_signal_add(&cursor->events.button, &cursor_button);
  wl_signal_add(&cursor->events.axis, &cursor_axis);
  wl_signal_add(&cursor->events.frame, &cursor_frame);

  cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
  wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

  wl_signal_add(&backend->events.new_input, &new_input_device);
  virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
  wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
                &new_virtual_keyboard);
  virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
  wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
                &new_virtual_pointer);

  pointer_gestures = wlr_pointer_gestures_v1_create(dpy);
  wl_signal_add(&cursor->events.swipe_begin, &swipebegin);
  wl_signal_add(&cursor->events.swipe_update, &swipeupdate);
  wl_signal_add(&cursor->events.swipe_end, &swipeend);
  wl_signal_add(&cursor->events.pinch_begin, &pinchbegin);
  wl_signal_add(&cursor->events.pinch_update, &pinchupdate);
  wl_signal_add(&cursor->events.pinch_end, &pinchend);
  wl_signal_add(&cursor->events.hold_begin, &holdbegin);
  wl_signal_add(&cursor->events.hold_end, &holdend);

  seat = wlr_seat_create(dpy, "seat0");
  wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
  wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
  wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
  wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
  wl_signal_add(&seat->events.start_drag, &start_drag);

  kb_group = createkeyboardgroup();
  wl_list_init(&kb_group->destroy.link);

  output_mgr = wlr_output_manager_v1_create(dpy);
  wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
  wl_signal_add(&output_mgr->events.test, &output_mgr_test);

  wl_global_create(dpy, &zdwl_ipc_manager_v2_interface, 2, NULL, dwl_ipc_manager_bind);
  unsetenv("DISPLAY");
#ifdef XWAYLAND
  if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
    wl_signal_add(&xwayland->events.ready, &xwayland_ready);
    wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);
    setenv("DISPLAY", xwayland->display_name, 1);
  } else {
    fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
  }
#endif
}

void spawn(const Arg *arg) {
  if (fork() == 0) {
    dup2(STDERR_FILENO, STDOUT_FILENO);
    setsid();
    execvp(((char **)arg->v)[0], (char **)arg->v);
    die("newl: execvp %s failed:", ((char **)arg->v)[0]);
  }
}

void startdrag(struct wl_listener *listener, void *data) {
  struct wlr_drag *drag = data;
  if (!drag->icon)
    return;

  drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
  LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void tag(const Arg *arg) {
  uint32_t newtags;
  Client *sel = focustop(selmon);
  if (!sel || !(newtags = arg->ui & TAGMASK))
    return;

  sel->tags = newtags;
  setmonitortags(selmon, newtags, 1);
  focusclient(sel, 1);
}

void tagmon(const Arg *arg) {
  Client *sel = focustop(selmon);
  if (sel)
    setmon(sel, dirtomon(arg->i), 0);
}

void tile(Monitor *m) {
  unsigned int i = 0, n = 0, mw, master_y, stack_y, remaining_height, remaining_windows, stack_height;
  Client *c;
  struct wlr_box target;

  wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
      n++;

  if (n == 0)
    return;
  if (n > 1)
    mw = (int)((m->w.width - m->gaps * enablegaps) * m->mfact);
  else
    mw = m->w.width - m->gaps * enablegaps;
  master_y = stack_y = m->w.y + m->gaps * enablegaps;

  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
      continue;
    if (i == 0) {
      target = (struct wlr_box){
          .x = m->w.x + m->gaps * enablegaps,
          .y = master_y,
          .width = mw - m->gaps * enablegaps,
          .height = m->w.height - 2 * m->gaps * enablegaps};
    } else {
      remaining_height = m->w.height - (stack_y - m->w.y) - m->gaps * enablegaps;
      remaining_windows = n - i;
      stack_height = (remaining_height - (remaining_windows - 1) * m->gaps * enablegaps) / remaining_windows;

      target = (struct wlr_box){
          .x = m->w.x + mw + m->gaps * enablegaps,
          .y = stack_y,
          .width = m->w.width - mw - 2 * m->gaps * enablegaps,
          .height = stack_height};

      stack_y += stack_height + m->gaps * enablegaps;
    }
    start_layout_animation(c, m, &target);
    i++;
  }
}

void togglefloating(const Arg *arg) {
  Client *sel = focustop(selmon);
  if (sel && !sel->isfullscreen)
    setfloating(sel, !sel->isfloating);
}

void togglefullscreen(const Arg *arg) {
  Client *sel = focustop(selmon);
  if (sel)
    setfullscreen(sel, !sel->isfullscreen);
}

void toggletag(const Arg *arg) {
  uint32_t newtags;
  Client *sel = focustop(selmon);
  if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
    return;

  sel->tags = newtags;
  focusclient(focustop(selmon), 1);
  arrange(selmon);
  printstatus();
}

void toggleview(const Arg *arg) {
  uint32_t newtagset;
  if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
    return;

  setmonitortags(selmon, newtagset, 0);
}

void unlocksession(struct wl_listener *listener, void *data) {
  SessionLock *lock = wl_container_of(listener, lock, unlock);
  destroylock(lock, 1);
}

void unmaplayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *l = wl_container_of(listener, l, unmap);

  if (l->mapped && l->scene) {
    sample_animation_state(&l->anim, NULL, &l->geom, &l->opacity, NULL, NULL);
    set_layer_surface_geom(l, &l->geom);
    apply_layer_surface_opacity(l, l->opacity);
    create_layer_surface_close_overlay(l, &l->geom);
  }

  l->anim.active = 0;
  l->mapped = 0;
  wlr_scene_node_set_enabled(&l->scene->node, 0);
  wlr_scene_node_set_enabled(&l->popups->node, 0);
  if (l == exclusive_focus)
    exclusive_focus = NULL;
  if (l->layer_surface->output && (l->mon = l->layer_surface->output->data)) {
    l->being_unmapped = 1;
    arrangelayers(l->mon);
    l->being_unmapped = 0;
  }
  if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
    focusclient(focustop(selmon), 1);
  motionnotify(0, NULL, 0, 0, 0, 0);
}

void unmapnotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, unmap);
  client_clear_pending_configure(c);
  if (c == grabc) {
    cursor_mode = CurNormal;
    grabc = NULL;
  }

  if (client_is_unmanaged(c)) {
    if (c == exclusive_focus) {
      exclusive_focus = NULL;
      focusclient(focustop(selmon), 1);
    }
  } else {
    if (!c->close_pending && c->scene) {
      sample_animation_state(&c->anim, NULL, &c->geom, &c->opacity, NULL, NULL);
      client_apply_scene_geometry(c, &c->geom);
      apply_client_opacity(c, c->opacity);
      create_close_overlay(c, &c->geom);
    }

    c->anim.active = 0;
    if (c == focus_anchor)
      focus_anchor = focus_fallback_from(c, c->mon);

    wl_list_remove(&c->link);
    setmon(c, NULL, 0);
  }

  wlr_scene_node_destroy(&c->scene->node);
  printstatus();
  motionnotify(0, NULL, 0, 0, 0, 0);
}

void updatemons(struct wl_listener *listener, void *data) {
  struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
  Client *c;
  struct wlr_output_configuration_head_v1 *config_head;
  Monitor *m;

  wl_list_for_each(m, &mons, link) {
    if (m->wlr_output->enabled || m->asleep)
      continue;
    config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
    config_head->state.enabled = 0;
    wlr_output_layout_remove(output_layout, m->wlr_output);
    closemon(m);
    m->m = m->w = (struct wlr_box){0};
  }
  wl_list_for_each(m, &mons, link) {
    if (m->wlr_output->enabled && !wlr_output_layout_get(output_layout, m->wlr_output))
      wlr_output_layout_add_auto(output_layout, m->wlr_output);
  }

  wlr_output_layout_get_box(output_layout, NULL, &sgeom);

  wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
  wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

  wl_list_for_each(m, &mons, link) {
    if (!m->wlr_output->enabled)
      continue;
    config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

    wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
    m->w = m->m;
    wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

    if (m->lock_surface) {
      struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
      wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
      wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
    }

    arrangelayers(m);
    arrange(m);
    if ((c = focustop(m)) && c->isfullscreen)
      client_request_geometry(c, &m->m, 0);

    m->gamma_lut_changed = 1;

    config_head->state.x = m->m.x;
    config_head->state.y = m->m.y;

    if (!selmon) {
      selmon = m;
    }
  }

  if (selmon && selmon->wlr_output->enabled) {
    wl_list_for_each(c, &clients, link) {
      if (!c->mon && client_surface(c)->mapped)
        setmon(c, selmon, c->tags);
    }
    focusclient(focustop(selmon), 1);
    if (selmon->lock_surface) {
      client_notify_enter(selmon->lock_surface->surface,
                          wlr_seat_get_keyboard(seat));
      client_activate_surface(selmon->lock_surface->surface, 1);
    }
  }
  wlr_cursor_move(cursor, NULL, 0, 0);
  wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void updatetitle(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, set_title);
  if (c == focustop(c->mon))
    printstatus();
}

void urgent(struct wl_listener *listener, void *data) {
  struct wlr_xdg_activation_v1_request_activate_event *event = data;
  Client *c = NULL;
  toplevel_from_wlr_surface(event->surface, &c, NULL);
  if (!c || c == focustop(selmon))
    return;

  c->isurgent = 1;
  printstatus();

  if (client_surface(c)->mapped)
    client_set_border_color(c, urgentcolor);
}

void view(const Arg *arg) {
  setmonitortags(selmon, arg->ui & TAGMASK, 1);
}

void virtualkeyboard(struct wl_listener *listener, void *data) {
  struct wlr_virtual_keyboard_v1 *kb = data;
  KeyboardGroup *group = createkeyboardgroup();
  wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
  LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);
  wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void virtualpointer(struct wl_listener *listener, void *data) {
  struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
  struct wlr_input_device *device = &event->new_pointer->pointer.base;

  wlr_cursor_attach_input_device(cursor, device);
  if (event->suggested_output)
    wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

Monitor *
xytomon(double x, double y) {
  struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
  return o ? o->data : NULL;
}

void xytonode(double x, double y, struct wlr_surface **psurface,
              Client **pc, LayerSurface **pl, double *nx, double *ny) {
  struct wlr_scene_node *node, *pnode;
  struct wlr_surface *surface = NULL;
  Client *c = NULL;
  LayerSurface *l = NULL;
  int layer;

  for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
    if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
      continue;

    if (node->type == WLR_SCENE_NODE_BUFFER)
      surface = wlr_scene_surface_try_from_buffer(
                    wlr_scene_buffer_from_node(node))
                    ->surface;
    for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
      c = pnode->data;
    if (c && c->type == LayerShell) {
      c = NULL;
      l = pnode->data;
    }
  }

  if (psurface)
    *psurface = surface;
  if (pc)
    *pc = c;
  if (pl)
    *pl = l;
}

void zoom(const Arg *arg) {
  Client *c, *sel = focustop(selmon);

  if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
    return;

  wl_list_for_each(c, &clients, link) {
    if (VISIBLEON(c, selmon) && !c->isfloating) {
      if (c != sel)
        break;
      sel = NULL;
    }
  }
  if (&c->link == &clients)
    return;
  if (!sel)
    sel = c;
  wl_list_remove(&sel->link);
  wl_list_insert(&clients, &sel->link);

  focusclient(sel, 1);
  arrange(selmon);
}

#ifdef XWAYLAND
void activatex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, activate);

  if (!client_is_unmanaged(c))
    wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void associatex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, associate);

  LISTEN(&client_surface(c)->events.commit, &c->commit, commitnotify);
  LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
  LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void configurex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, configure);
  struct wlr_xwayland_surface_configure_event *event = data;
  struct wlr_box geo = {
      .x = event->x - (int)c->bw,
      .y = event->y - (int)c->bw,
      .width = event->width + 2 * c->bw,
      .height = event->height + 2 * c->bw,
  };

  client_apply_x11_configure_request(c, event, &geo);
}

void createnotifyx11(struct wl_listener *listener, void *data) {
  struct wlr_xwayland_surface *xsurface = data;
  Client *c;
  int unmanaged;

  c = xsurface->data = ecalloc(1, sizeof(*c));
  c->surface.xwayland = xsurface;
  unmanaged = client_is_unmanaged(c);
  client_init_common(c, X11, unmanaged ? 0 : borderpx, !unmanaged);

  LISTEN(&xsurface->events.associate, &c->associate, associatex11);
  LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
  LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
  LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
  LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
  LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
  LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
  LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void dissociatex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, dissociate);
  if (!wl_list_empty(&c->commit.link)) {
    wl_list_remove(&c->commit.link);
    wl_list_init(&c->commit.link);
  }
  wl_list_remove(&c->map.link);
  wl_list_init(&c->map.link);
  wl_list_remove(&c->unmap.link);
  wl_list_init(&c->unmap.link);
}

void sethints(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, set_hints);
  struct wlr_surface *surface = client_surface(c);
  if (c == focustop(selmon) || !c->surface.xwayland->hints)
    return;

  c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
  printstatus();

  if (c->isurgent && surface && surface->mapped)
    client_set_border_color(c, urgentcolor);
}

void xwaylandready(struct wl_listener *listener, void *data) {
  struct wlr_xcursor *xcursor;
  wlr_xwayland_set_seat(xwayland, seat);
  if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
    wlr_xwayland_set_cursor(xwayland,
                            wlr_xcursor_image_get_buffer(xcursor->images[0]),
                            xcursor->images[0]->hotspot_x,
                            xcursor->images[0]->hotspot_y);
}
#endif

int main(int argc, char *argv[]) {
  char *startup_cmd = NULL;
  int c;

  while ((c = getopt(argc, argv, "s:hdv")) != -1) {
    if (c == 's')
      startup_cmd = optarg;
    else if (c == 'd')
      log_level = WLR_DEBUG;
    else if (c == 'v')
      die("newl " VERSION);
  }
  if (!getenv("XDG_RUNTIME_DIR"))
    die("XDG_RUNTIME_DIR must be set");
  setup();
  run(startup_cmd);
  cleanup();
  return EXIT_SUCCESS;
}
