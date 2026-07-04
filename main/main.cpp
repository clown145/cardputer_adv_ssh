#include "app/app_controller.h"

extern "C" void app_main(void)
{
    auto* app = new adv::AppController();
    app->run();
}
