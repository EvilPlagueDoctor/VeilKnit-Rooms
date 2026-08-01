#include "room_engine.hpp"
#include "util.hpp"

#include <gtk/gtk.h>
#include <atomic>
#include <memory>
#include <string>

namespace {
struct App {
    GtkWidget* window{};
    GtkWidget* status{};
    GtkWidget* account{};
    GtkWidget* room_list{};
    GtkWidget* messages{};
    GtkWidget* compose{};
    GtkWidget* create_name{};
    GtkWidget* join_code{};
    std::unique_ptr<vkrooms::RoomEngine> engine;
    std::atomic<bool> refresh_pending{false};
};

void set_text(GtkWidget* label, const std::string& text) {
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
}

void render(App* app) {
    const auto snapshot = app->engine->snapshot();
    set_text(app->status, snapshot.status);
    set_text(app->account, snapshot.username.empty() ? "No daemon account connected" : "Connected account: " + snapshot.username);

    gtk_list_box_invalidate_filter(GTK_LIST_BOX(app->room_list));
    GList* children = gtk_container_get_children(GTK_CONTAINER(app->room_list));
    for (GList* node = children; node; node = node->next) gtk_widget_destroy(GTK_WIDGET(node->data));
    g_list_free(children);
    for (std::size_t i = 0; i < snapshot.rooms.size(); ++i) {
        const auto& room = snapshot.rooms[i];
        std::string name = room.name;
        if (room.suspended) name += " [suspended]";
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* label = gtk_label_new(name.c_str());
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(app->room_list), row, -1);
        if (static_cast<int>(i) == snapshot.selected_room) gtk_list_box_select_row(GTK_LIST_BOX(app->room_list), GTK_LIST_BOX_ROW(row));
    }

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->messages));
    std::string text;
    if (snapshot.selected_room >= 0 && snapshot.selected_room < static_cast<int>(snapshot.rooms.size())) {
        const auto& room = snapshot.rooms[static_cast<std::size_t>(snapshot.selected_room)];
        for (const auto& message : room.messages) {
            if (message.deleted) continue;
            text += '[' + vkrooms::format_time(message.created_at) + "] ";
            text += message.sender_name.empty() ? vkrooms::short_identity(message.sender_main_dht) : message.sender_name;
            text += ": " + message.text;
            if (message.pending) text += " [pending]";
            text += '\n';
        }
    }
    gtk_text_buffer_set_text(buffer, text.c_str(), static_cast<gint>(text.size()));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->messages), &end, 0.0, FALSE, 0.0, 0.0);
}

gboolean refresh_idle(gpointer data) {
    auto* app = static_cast<App*>(data);
    app->refresh_pending.store(false, std::memory_order_release);
    render(app);
    return G_SOURCE_REMOVE;
}

void schedule_refresh(App* app) {
    bool expected = false;
    if (app->refresh_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        g_idle_add(refresh_idle, app);
    }
}

std::string entry(GtkWidget* widget) { return gtk_entry_get_text(GTK_ENTRY(widget)); }
void clear(GtkWidget* widget) { gtk_entry_set_text(GTK_ENTRY(widget), ""); }

void on_room_selected(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    if (!row) return;
    static_cast<App*>(data)->engine->select_room(gtk_list_box_row_get_index(row));
}
void on_send(GtkWidget*, gpointer data) {
    auto* app=static_cast<App*>(data); const auto value=entry(app->compose); if (!value.empty()) { app->engine->send_chat(value); clear(app->compose); }
}
void on_create(GtkWidget*, gpointer data) {
    auto* app=static_cast<App*>(data); const auto value=entry(app->create_name); if (!value.empty()) { app->engine->create_room(value); clear(app->create_name); }
}
void on_join(GtkWidget*, gpointer data) {
    auto* app=static_cast<App*>(data); const auto value=entry(app->join_code); if (!value.empty()) { app->engine->join_room(value); clear(app->join_code); }
}
void on_connect(GtkWidget*, gpointer data) { static_cast<App*>(data)->engine->reconnect_daemon(false); }
void on_fresh(GtkWidget*, gpointer data) { static_cast<App*>(data)->engine->reconnect_daemon(true); }
void on_sync(GtkWidget*, gpointer data) { static_cast<App*>(data)->engine->sync_selected_room(); }
void on_replica(GtkWidget*, gpointer data) { static_cast<App*>(data)->engine->toggle_replica(); }
void on_leave(GtkWidget*, gpointer data) { static_cast<App*>(data)->engine->remove_selected_room(); }
void on_copy_invite(GtkWidget*, gpointer data) {
    auto* app=static_cast<App*>(data);
    const auto code=app->engine->selected_invite_code();
    GtkClipboard* clipboard=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, code.c_str(), static_cast<gint>(code.size()));
}
void on_destroy(GtkWidget*, gpointer data) {
    auto* app=static_cast<App*>(data); app->engine->stop(); gtk_main_quit();
}

GtkWidget* button(const char* text, GCallback callback, App* app) {
    GtkWidget* value=gtk_button_new_with_label(text); g_signal_connect(value,"clicked",callback,app); return value;
}
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    App app;
    app.engine=std::make_unique<vkrooms::RoomEngine>([&app]{ schedule_refresh(&app); }, [](const std::string& line){ g_message("%s", line.c_str()); });
    app.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "VeilKnit Rooms");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1080, 720);
    g_signal_connect(app.window,"destroy",G_CALLBACK(on_destroy),&app);

    GtkWidget* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(root),10); gtk_container_add(GTK_CONTAINER(app.window),root);
    GtkWidget* top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(root),top,FALSE,FALSE,0);
    app.status=gtk_label_new("Starting..."); gtk_widget_set_halign(app.status,GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(top),app.status,TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(top),button("Reconnect",G_CALLBACK(on_connect),&app),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(top),button("Fresh connect",G_CALLBACK(on_fresh),&app),FALSE,FALSE,0);
    app.account=gtk_label_new(""); gtk_widget_set_halign(app.account,GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(root),app.account,FALSE,FALSE,0);

    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(root),actions,FALSE,FALSE,0);
    app.create_name=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.create_name),"New room name"); gtk_box_pack_start(GTK_BOX(actions),app.create_name,TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Create",G_CALLBACK(on_create),&app),FALSE,FALSE,0);
    app.join_code=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.join_code),"Invite code"); gtk_box_pack_start(GTK_BOX(actions),app.join_code,TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(actions),button("Join",G_CALLBACK(on_join),&app),FALSE,FALSE,0);

    GtkWidget* pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_box_pack_start(GTK_BOX(root),pane,TRUE,TRUE,0);
    app.room_list=gtk_list_box_new(); gtk_widget_set_size_request(app.room_list,240,-1); g_signal_connect(app.room_list,"row-selected",G_CALLBACK(on_room_selected),&app);
    GtkWidget* room_scroll=gtk_scrolled_window_new(nullptr,nullptr); gtk_container_add(GTK_CONTAINER(room_scroll),app.room_list); gtk_paned_pack1(GTK_PANED(pane),room_scroll,FALSE,FALSE);
    GtkWidget* right=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_paned_pack2(GTK_PANED(pane),right,TRUE,FALSE);
    app.messages=gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(app.messages),FALSE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app.messages),GTK_WRAP_WORD_CHAR);
    GtkWidget* msg_scroll=gtk_scrolled_window_new(nullptr,nullptr); gtk_container_add(GTK_CONTAINER(msg_scroll),app.messages); gtk_box_pack_start(GTK_BOX(right),msg_scroll,TRUE,TRUE,0);
    GtkWidget* room_actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(right),room_actions,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(room_actions),button("Sync",G_CALLBACK(on_sync),&app),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(room_actions),button("Copy invite",G_CALLBACK(on_copy_invite),&app),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(room_actions),button("Toggle replica",G_CALLBACK(on_replica),&app),FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(room_actions),button("Leave",G_CALLBACK(on_leave),&app),FALSE,FALSE,0);
    GtkWidget* compose_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(right),compose_row,FALSE,FALSE,0);
    app.compose=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.compose),"Message"); g_signal_connect(app.compose,"activate",G_CALLBACK(on_send),&app); gtk_box_pack_start(GTK_BOX(compose_row),app.compose,TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(compose_row),button("Send",G_CALLBACK(on_send),&app),FALSE,FALSE,0);

    app.engine->start(); app.engine->connect_async();
    gtk_widget_show_all(app.window); render(&app); gtk_main();
    return 0;
}
