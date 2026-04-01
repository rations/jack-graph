#include <gtkmm.h>
#include "JackGraph.hpp"

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "org.jackgraph");

    JackGraph window;

    return app->run(window);
}
