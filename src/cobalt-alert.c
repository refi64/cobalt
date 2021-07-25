/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "cobalt-alert.h"

#include "cobalt-resources.h"

typedef enum MarkupType MarkupType;

enum MarkupType { MARKUP_PLAIN, MARKUP_HEADER, MARKUP_CODE };

#define ROOT_TAG "content"

struct _CobaltAlert {
  GtkDialog parent_instance;
};

G_DEFINE_TYPE(CobaltAlert, cobalt_alert, GTK_TYPE_DIALOG)

static void cobalt_alert_class_init(CobaltAlertClass *alert_class) {}

static void cobalt_alert_init(CobaltAlert *alert) {}

typedef struct ContentParseData ContentParseData;

struct ContentParseData {
  GtkContainer *target;
  GString *markup;
};

static void content_parse_data_clear(ContentParseData *data) {
  if (data->markup != NULL) {
    g_string_free(g_steal_pointer(&data->markup), TRUE);
  }
}

static gboolean markup_type_for_name(const char *name, MarkupType *out_type) {
  MarkupType type;

  if (g_str_equal(name, "header")) {
    type = MARKUP_HEADER;
  } else if (g_str_equal(name, "code")) {
    type = MARKUP_CODE;
  } else if (g_str_equal(name, "markup")) {
    type = MARKUP_PLAIN;
  } else {
    return FALSE;
  }

  if (out_type != NULL) {
    *out_type = type;
  }

  return TRUE;
}

static void content_parser_start_element(GMarkupParseContext *context, const char *name,
                                         const char **attr_names,
                                         const char **attr_values, gpointer user_data,
                                         GError **error) {
  ContentParseData *data = user_data;

  if (data->markup != NULL) {
    g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "Nested markup is not supported");
    return;
  }

  if (g_str_equal(name, ROOT_TAG)) {
    return;
  } else if (!markup_type_for_name(name, NULL)) {
    g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                "Unknown element '%s'", name);
    return;
  }

  data->markup = g_string_new("");
}

static void content_parser_end_element(GMarkupParseContext *context, const char *name,
                                       gpointer user_data, GError **error) {
  ContentParseData *data = user_data;

  if (g_str_equal(name, ROOT_TAG)) {
    return;
  }

  MarkupType type = MARKUP_PLAIN;
  g_warn_if_fail(markup_type_for_name(name, &type));

  g_autofree char *markup = g_string_free(g_steal_pointer(&data->markup), FALSE);
  g_strchomp(markup);

  GtkWidget *widget = gtk_label_new("");
  GtkLabel *label = GTK_LABEL(widget);
  gtk_label_set_selectable(label, TRUE);
  gtk_label_set_xalign(label, 0);
  if (type == MARKUP_HEADER) {
    gtk_widget_set_halign(widget, GTK_ALIGN_CENTER);
  }

  gboolean needs_wrapper_tags = type == MARKUP_CODE || type == MARKUP_HEADER;
  if (needs_wrapper_tags) {
    g_autofree char *escaped_markup = g_markup_escape_text(markup, -1);
    g_clear_pointer(&markup, g_free);
    markup = g_strdup_printf(type == MARKUP_CODE ? "<tt>%s</tt>" : "<b><big>%s</big></b>",
                             escaped_markup);
  }

  gtk_label_set_markup(label, markup);

  if (type == MARKUP_CODE) {
    // Code blocks should be scrollable.
    GtkWidget *label_widget = g_steal_pointer(&widget);
    widget = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_NEVER);
    gtk_container_add(GTK_CONTAINER(widget), label_widget);

    // Add some top/bottom margins so the scrollbar doesn't cover the text.
    gtk_widget_set_margin_top(label_widget, 8);
    gtk_widget_set_margin_bottom(label_widget, 8);

    // Add some start/end margins, but to the scrolled window itself instead of
    // the label, otherwise the adjustment ends up auto-scrolling past the
    // margins (for unknown reasons).
    gtk_widget_set_margin_start(widget, 8);
    gtk_widget_set_margin_end(widget, 8);
  } else {
    // Headers & plain text should wrap normally.
    gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
  }

  gtk_container_add(data->target, widget);
}

static void content_parser_text(GMarkupParseContext *context, const char *text, gsize len,
                                gpointer user_data, GError **error) {
  ContentParseData *data = user_data;

  if (data->markup == NULL) {
    return;
  }

  const char *tag = g_markup_parse_context_get_element_stack(context)->data;
  MarkupType type = MARKUP_PLAIN;
  g_warn_if_fail(markup_type_for_name(tag, &type));

  const char *text_end = text + len;
  while (text < text_end) {
    // Skip the leading whitespace on each line.
    while (text < text_end && *text == ' ') {
      text++;
    }

    if (text >= text_end) {
      break;
    }

    const char *line_end = strchr(text, '\n');

    // If we're at EOF (i.e. there are no more newlines), the text end is
    // treated as the line end.
    gsize line_length = (line_end != NULL ? line_end : text_end) - text;

    // Skip blank / newline-only lines.
    if (line_length == 0) {
      if (line_end != NULL) {
        text = line_end + 1;
      }

      continue;
    }

    if (line_end != NULL && type == MARKUP_CODE) {
      // Preserve the newlines for code blocks.
      line_length++;
    }

    g_string_append_len(data->markup, text, line_length);
    text += line_length;

    if (line_end != NULL && type != MARKUP_CODE) {
      // Non-code gets its lines separated by spaces.
      g_string_append_c(data->markup, ' ');
    }
  }
}

static gboolean insert_content_widgets(GtkContainer *target, const char *content,
                                       GError **error) {
  ContentParseData data = {
      .target = target,
      .markup = NULL,
  };
  GMarkupParser parser = {
      .start_element = content_parser_start_element,
      .end_element = content_parser_end_element,
      .text = content_parser_text,
  };

  g_autoptr(GMarkupParseContext) context =
      g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &data,
                                 (GDestroyNotify)content_parse_data_clear);
  if (!g_markup_parse_context_parse(context, content, -1, error) ||
      !g_markup_parse_context_end_parse(context, error)) {
    return FALSE;
  }

  return TRUE;
}

static void insert_content_widgets_from_resource(GtkContainer *target, const char *path) {
  GResource *resource = cobalt_get_resource();
  g_autoptr(GBytes) content_bytes =
      g_resource_lookup_data(resource, path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  g_warn_if_fail(content_bytes != NULL);

  g_autofree char *content =
      g_strndup(g_bytes_get_data(content_bytes, NULL), g_bytes_get_size(content_bytes));
  g_autoptr(GError) local_error = NULL;

  if (!insert_content_widgets(target, content, &local_error)) {
    g_critical("Failed to parse resource '%s': %s", path, local_error->message);
  }
}

static gboolean reset_adjusment(gpointer user_data) {
  GtkAdjustment *adjustment = user_data;
  gtk_adjustment_set_value(adjustment, 0);
  return FALSE;
}

static void cobalt_alert_late_init(CobaltAlert *alert, const char *title,
                                   GList *resource_paths) {
  GtkDialog *alert_dialog = GTK_DIALOG(alert);
  GtkWindow *alert_window = GTK_WINDOW(alert);

  gtk_window_set_default_size(alert_window, 640, 480);
  gtk_window_set_title(alert_window, title);

  GtkWidget *scroll_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_start(scroll_box, 16);
  gtk_widget_set_margin_end(scroll_box, 16);
  gtk_widget_set_margin_top(scroll_box, 16);
  gtk_widget_set_margin_bottom(scroll_box, 16);
  gtk_box_set_spacing(GTK_BOX(scroll_box), 16);

  for (; resource_paths != NULL; resource_paths = resource_paths->next) {
    insert_content_widgets_from_resource(GTK_CONTAINER(scroll_box), resource_paths->data);
  }

  GtkWidget *scroll_area = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_area), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll_area), scroll_box);

  GtkWidget *content_area = gtk_dialog_get_content_area(alert_dialog);
  gtk_box_pack_start(GTK_BOX(content_area), scroll_area, TRUE, TRUE, 0);
  gtk_widget_show_all(content_area);

  // For unknown reasons, the adjusment value gets bumped up as the labels have
  // their sizes filled in, so we reset it on the next main loop iteration.
  g_idle_add(reset_adjusment,
             gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll_area)));

  gtk_dialog_add_button(alert_dialog, "OK", GTK_RESPONSE_OK);
}

CobaltAlert *cobalt_alert_new_from_resources(const char *title, ...) {
  va_list va;
  va_start(va, title);

  g_autoptr(GList) resource_paths = NULL;
  const char *path = NULL;
  while ((path = va_arg(va, const char *)) != NULL) {
    resource_paths = g_list_append(resource_paths, (gpointer)path);
  }

  CobaltAlert *alert = g_object_new(COBALT_TYPE_ALERT, NULL);
  cobalt_alert_late_init(alert, title, resource_paths);
  return alert;
}
