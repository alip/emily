/* Emily tries but misunderstands...
 * vim: set et ts=4 sts=4 sw=4 fdm=syntax :
 * Copyright 2009 Ali Polatel <polatel@gmail.com>
 * Distributed under the terms of the GNU General Public License v2
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <cairo.h>
#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define PACKAGE "emily"
#endif /* HAVE_CONFIG_H */

#define MAIN_WIDGET_TITLE   "Emily - the trippy chess interface"

/* Colours */
#define WHITE       1
#define BLACK       2
#define MAX_COLOUR  3

/* Pieces */
#define PAWN        1
#define KNIGHT      2
#define BISHOP      3
#define ROOK        4
#define QUEEN       5
#define KING        6
#define MAX_PIECE   7

#define BRANK(sq)    ((sq) >> 3)
#define BFILE(sq)    ((sq) & 7)

static struct _globalconf {
    /* Command line options */
    gint verbose;
    gint print_version;
    gint piece_dimension;
    gint board_margin;
    gchar *theme;

    /* Variables */
    gint flip;
    cairo_t *board_renderer;
    GtkWidget *main_widget;
    GtkWidget *main_vbox;
    GtkWidget *board_widget;
    GtkWidget *statusbar_widget;
    guint statusbar_id;
} globalconf;

#define BOARD_WIDTH()   (8 * (globalconf.piece_dimension) + 2 * (globalconf.board_margin))
#define BOARD_HEIGHT()  BOARD_WIDTH()

const GOptionEntry entries[] = {
    { "verbose",    'v', 0, G_OPTION_ARG_NONE, &globalconf.verbose, "Be verbose", NULL },
    { "version",    'V', 0, G_OPTION_ARG_NONE, &globalconf.print_version, "Print version and exit", NULL },
    { "set",        's', 0, G_OPTION_ARG_STRING, &globalconf.theme, "Path to the piece set", NULL },
    { "dimension",  'd', 0, G_OPTION_ARG_INT, &globalconf.piece_dimension, "Piece dimension", NULL },
    { "margin",     'm', 0, G_OPTION_ARG_INT, &globalconf.board_margin, "Board margin", NULL },
    { NULL,          0,  0, 0, NULL, NULL, NULL},
};

static void __lg(const char *func, size_t len, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
static void __lg(const char *func, size_t len, const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, PACKAGE"@%ld: [%s:%zu] ", time(NULL), func, len);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
#define lg(...)     __lg(__func__, __LINE__, __VA_ARGS__)
#define lgv(...)                                        \
        do {                                            \
            if (globalconf.verbose)                     \
                __lg(__func__, __LINE__, __VA_ARGS__);  \
        } while (0)

static void about(void)
{
    fprintf(stderr, PACKAGE"-"VERSION);
#ifdef GITHEAD
    if (0 != strlen(GITHEAD))
        fprintf(stderr, "-"GITHEAD);
#endif
    fputc('\n', stderr);
}

static void cleanup(void)
{
    g_free(globalconf.theme);
    cairo_destroy(globalconf.board_renderer);
}

static gboolean set_piece_from_svg(int square, int colour, int piece, GError **load_error)
{
    int rank, file;
    double tx, ty;
    GString *piece_path;
    RsvgHandle *handle;

    piece_path = g_string_new(globalconf.theme);
    g_string_append_printf(piece_path,
            G_DIR_SEPARATOR_S "%d" G_DIR_SEPARATOR_S "%c", globalconf.piece_dimension,
            (WHITE == colour) ? 'w' : 'b');
    switch (piece) {
        case PAWN:
            g_string_append_c(piece_path, 'p');
            break;
        case KNIGHT:
            g_string_append_c(piece_path, 'n');
            break;
        case BISHOP:
            g_string_append_c(piece_path, 'b');
            break;
        case ROOK:
            g_string_append_c(piece_path, 'r');
            break;
        case QUEEN:
            g_string_append_c(piece_path, 'q');
            break;
        case KING:
            g_string_append_c(piece_path, 'k');
            break;
        default:
            g_assert_not_reached();
            break;
    }
    g_string_append(piece_path, ".svg");

    lgv("Loading SVG from `%s'", piece_path->str);
    handle = rsvg_handle_new_from_file(piece_path->str, load_error);
    if (NULL != *load_error) {
        g_string_free(piece_path, TRUE);
        rsvg_handle_free(handle);
        return FALSE;
    }
    lgv("Done loading from `%s', closing SVG handle", piece_path->str);
    if (!rsvg_handle_close(handle, load_error)) {
        g_string_free(piece_path, TRUE);
        rsvg_handle_free(handle);
        return FALSE;
    }

    rank = BRANK(square);
    file = BFILE(square);
    lgv("Placing piece, rank: %d, file: %d", rank, file);

    tx = ty = 0.0;
    if (!globalconf.flip)
        rank = 7 - rank;

    for (; rank > 0; rank--)
        ty += globalconf.piece_dimension;
    for (; file > 0; file--)
        tx += globalconf.piece_dimension;

    lgv("Translating user-space origin to (%.2f, %.2f)", tx, ty);
    cairo_translate(globalconf.board_renderer, tx, ty);

    lgv("Rendering piece from `%s'", piece_path->str);
    rsvg_handle_render_cairo(handle, globalconf.board_renderer);

    cairo_translate(globalconf.board_renderer, -tx, -ty);

    g_string_free(piece_path, TRUE);
    rsvg_handle_free(handle);
    return TRUE;
}

static void xset_piece_from_svg(int square, int colour, int piece)
{
    GError *piece_error = NULL;

    if (!set_piece_from_svg(square, colour, piece, &piece_error)) {
        g_printerr("Error loading SVG: %s\n", piece_error->message);
        g_error_free(piece_error);
        cleanup();
        exit(EXIT_FAILURE);
    }
}

static void on_destroy(GtkWidget *widget G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gtk_main_quit();
}

static gboolean on_expose_event(GtkWidget *widget, GdkEventExpose *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    int x, y, sq;

    globalconf.board_renderer = gdk_cairo_create(widget->window);

    /* Draw the big black rectangle and fill it */
    cairo_set_source_rgb(globalconf.board_renderer, 0.5, 0.5, 0.5);
    cairo_rectangle(globalconf.board_renderer, 0, 0, BOARD_WIDTH(), BOARD_HEIGHT());
    cairo_fill(globalconf.board_renderer);

    /* Draw white squares */
    cairo_set_source_rgb(globalconf.board_renderer, 1, 1, 1);
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            if ((x & 1) == (y & 1)) {
                cairo_rectangle(globalconf.board_renderer,
                        globalconf.board_margin + x * globalconf.piece_dimension,
                        globalconf.board_margin + y * globalconf.piece_dimension,
                        globalconf.piece_dimension, globalconf.piece_dimension);
                cairo_fill(globalconf.board_renderer);
            }
        }
    }

    /* Place pieces */
    for (sq = 8; sq < 16; sq++)
        xset_piece_from_svg(sq, WHITE, PAWN);
    for (sq = 48; sq < 56; sq++)
        xset_piece_from_svg(sq, BLACK, PAWN);
    for (sq = 0; sq < 8; sq += 7)
        xset_piece_from_svg(sq, WHITE, ROOK);
    for (sq = 56; sq < 64; sq += 7)
        xset_piece_from_svg(sq, BLACK, ROOK);
    for (sq = 1; sq < 7; sq += 5)
        xset_piece_from_svg(sq, WHITE, KNIGHT);
    for (sq = 57; sq < 63; sq += 5)
        xset_piece_from_svg(sq, BLACK, KNIGHT);
    for (sq = 2; sq < 6; sq += 3)
        xset_piece_from_svg(sq, WHITE, BISHOP);
    for (sq = 58; sq < 62; sq += 3)
        xset_piece_from_svg(sq, BLACK, BISHOP);
    xset_piece_from_svg(3, WHITE, QUEEN);
    xset_piece_from_svg(4, WHITE, KING);
    xset_piece_from_svg(59, BLACK, QUEEN);
    xset_piece_from_svg(60, BLACK, KING);

    return FALSE;
}

static void create_main_widget(void)
{
    /* Create the widget and set properties */
    globalconf.main_widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(globalconf.main_widget), MAIN_WIDGET_TITLE);
    gtk_window_set_resizable(GTK_WINDOW(globalconf.main_widget), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(globalconf.main_widget), 10);

    /* Add callbacks */
    g_signal_connect(G_OBJECT(globalconf.main_widget), "destroy", G_CALLBACK(on_destroy), NULL);
}

static void create_main_vbox(void)
{
    globalconf.main_vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_add(GTK_CONTAINER(globalconf.main_widget), globalconf.main_vbox);
}

static void create_board_widget(void)
{
    /* Create the widget and set properties */
    globalconf.board_widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(globalconf.board_widget, BOARD_WIDTH(), BOARD_HEIGHT());

    /* Add callbacks */
    g_signal_connect(G_OBJECT(globalconf.board_widget), "expose_event", G_CALLBACK(on_expose_event), NULL);

    /* Add the drawing area to main vbox. */
    gtk_box_pack_start(GTK_BOX(globalconf.main_vbox), globalconf.board_widget, TRUE, TRUE, 2);
}

static void create_statusbar(void)
{
    /* Create the widget and set properties */
    globalconf.statusbar_widget = gtk_statusbar_new();
    globalconf.statusbar_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(globalconf.statusbar_widget),
            MAIN_WIDGET_TITLE);
    gtk_statusbar_push(GTK_STATUSBAR(globalconf.statusbar_widget), globalconf.statusbar_id, MAIN_WIDGET_TITLE);

    /* Add the statusbar to main vbox. */
    gtk_box_pack_end(GTK_BOX(globalconf.main_vbox), globalconf.statusbar_widget, FALSE, FALSE, 0);
}

static void show_widgets(void)
{
    gtk_widget_show(globalconf.statusbar_widget);
    gtk_widget_show(globalconf.board_widget);
    gtk_widget_show(globalconf.main_vbox);
    gtk_widget_show(globalconf.main_widget);
}

int main(int argc, char **argv)
{
    gchar *svg_subdir;
    GOptionContext *context;
    GError *parse_error = NULL;

    memset(&globalconf, 0, sizeof(struct _globalconf));

    /* Set defaults */
    globalconf.verbose = 0;
    globalconf.print_version = 0;
    globalconf.piece_dimension = 45;
    globalconf.board_margin = 2;
    globalconf.theme = NULL;
    globalconf.flip = 0;

    /* Parse arguments */
    context = g_option_context_new("");
    g_option_context_set_summary(context, PACKAGE "-" VERSION " - the trippy chess interface");
    g_option_context_add_main_entries(context, entries, PACKAGE);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(context, &argc, &argv, NULL)) {
        g_printerr("option parsing failed: %s\n", parse_error->message);
        g_error_free(parse_error);
        g_option_context_free(context);
        return EXIT_FAILURE;
    }
    g_option_context_free(context);

    if (globalconf.print_version) {
        about();
        return EXIT_SUCCESS;
    }

    if (NULL == globalconf.theme) {
        svg_subdir = g_strdup_printf("svg" G_DIR_SEPARATOR_S "%d", globalconf.piece_dimension);
        globalconf.theme = g_build_filename(PKGDATADIR, svg_subdir, NULL);
        g_free(svg_subdir);
    }

    create_main_widget();
    create_main_vbox();
    create_board_widget();
    create_statusbar();
    show_widgets();

    /* Enter main loop */
    gtk_main();

    cleanup();
    return EXIT_SUCCESS;
}

