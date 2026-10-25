#include    <MouseControlHandler.h>

#include    <VehiclesHandler.h>
#include    <Logger.h>

#include    <vsg/utils/LineSegmentIntersector.h>

#include    <algorithm>
#include    <cstdlib>

//------------------------------------------------------------------------------
// Отладочная печать нажатий и разбора пика.
// По умолчанию выключена, включается переменной окружения RRS_MCH_DEBUG=1.
//------------------------------------------------------------------------------
static bool mch_debug()
{
    static const bool enabled = (std::getenv("RRS_MCH_DEBUG") != nullptr);

    return enabled;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
MouseControlHandler::MouseControlHandler(vsg::ref_ptr<vsg::Camera> camera,
                                         vsg::ref_ptr<vsg::Keyboard> keyboard,
                                         VehiclesHandler *vehicles_handler)
    : _camera(camera)
    , _keyboard(keyboard)
    , _vehicles_handler(vehicles_handler)
{

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MouseControlHandler::apply(vsg::FrameEvent &frameEvent)
{
    (void) frameEvent;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MouseControlHandler::apply(vsg::MoveEvent &moveEvent)
{
    _pointer_x = moveEvent.x;
    _pointer_y = moveEvent.y;
    _pointer_valid = true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MouseControlHandler::apply(vsg::ButtonPressEvent &buttonPress)
{
    if (mch_debug())
    {
        LOG_INFO("MCH-DBG: press x=%d y=%d button=%d handled=%d",
                 static_cast<int>(buttonPress.x), static_cast<int>(buttonPress.y),
                 static_cast<int>(buttonPress.button), static_cast<int>(buttonPress.handled));
    }

    if (buttonPress.handled)
    {
        return;
    }

    io_control_input_t input;
    IOController *io_controller = nullptr;

    // Определяем что мы попали в какой-то орган управления,
    // возвращаем I/O-контроллер его обрабатываниющий и описатель сигнала
    if (pickControl(static_cast<int>(buttonPress.x),
                    static_cast<int>(buttonPress.y),
                    io_controller,
                    input))
    {
        // Если сигнал или контроллер невалидны - уходим
        if (input.id == 0 || io_controller == nullptr)
        {
            return;
        }

        // Передаем в данные обработчику нажатия кнопки мыши
        io_controller->mouseInputProcess(input, buttonPress.button, true);

        // Помечаем нажатие как обработанное
        buttonPress.handled = true;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MouseControlHandler::apply(vsg::ButtonReleaseEvent &buttonRelease)
{
    if (mch_debug())
    {
        LOG_INFO("MCH-DBG: release x=%d y=%d button=%d handled=%d",
                 static_cast<int>(buttonRelease.x), static_cast<int>(buttonRelease.y),
                 static_cast<int>(buttonRelease.button), static_cast<int>(buttonRelease.handled));
    }

    if (buttonRelease.handled)
    {
        return;
    }

    io_control_input_t input;
    IOController *io_controller = nullptr;

    if (pickControl(static_cast<int>(buttonRelease.x),
                    static_cast<int>(buttonRelease.y),
                    io_controller,
                    input))
    {
        if (input.id == 0 || io_controller == nullptr)
        {
            return;
        }

        io_controller->mouseInputProcess(input, buttonRelease.button, false);

        buttonRelease.handled = true;
    }
    else
    {
        // Курсор отпущен вне органа управления (или луч не попал в него):
        // сбрасываем все кнопки, иначе они остаются «зажатыми» навсегда.
        VehicleExterior *vehicle = _vehicles_handler->getCurrentVehicle();

        if (vehicle != nullptr)
        {
            for (auto *io_ctrl : vehicle->io_controls)
            {
                if (io_ctrl != nullptr)
                {
                    io_ctrl->releaseButtons(buttonRelease.button);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MouseControlHandler::pickControl(int x,
                                      int y,
                                      IOController* &io_ctrl,
                                      io_control_input_t &input)
{
    VehicleExterior *vehicle = _vehicles_handler->getCurrentVehicle();

    if (vehicle == nullptr || vehicle->io_controls.empty() || (_camera == nullptr))
    {
        return false;
    }

    static const int offsets[5][2] = {
        {0, 0}, {9, 0}, {-9, 0}, {0, -9}
    };

    vsg::ref_ptr<vsg::LineSegmentIntersector> intersector;

    for (auto &off : offsets)
    {
        intersector = vsg::LineSegmentIntersector::create(*_camera, x + off[0], y + off[1]);
        vehicle->transform->accept(*intersector);

        // Порядок пересечений в LineSegmentIntersector не гарантирован, а ratio
        // для пересечений с треугольниками не заполняется (всегда 0). Поэтому
        // сортируем по расстоянию от точки зрения до мировой точки пересечения:
        // иначе клик может "попасть" в дальний объект (соседнюю клавишу).
        const vsg::dmat4 viewMatrix = _camera->viewMatrix->transform();
        const vsg::dvec4 eye4 = vsg::inverse(viewMatrix) * vsg::dvec4(0.0, 0.0, 0.0, 1.0);
        const vsg::dvec3 eye(eye4.x, eye4.y, eye4.z);

        auto distance2 = [&eye](const vsg::dvec3 &p)
        {
            const vsg::dvec3 d = p - eye;
            return d.x * d.x + d.y * d.y + d.z * d.z;
        };

        auto &hits = intersector->intersections;
        std::sort(hits.begin(), hits.end(),
                  [&distance2](const auto &a, const auto &b)
                  {
                      return distance2(a->worldIntersection) < distance2(b->worldIntersection);
                  });

        if (mch_debug())
        {
            LOG_INFO("MCH-DBG: pick x=%d y=%d hits=%d", x + off[0], y + off[1],
                     static_cast<int>(intersector->intersections.size()));

            if (off[0] == 0 && off[1] == 0)
            {
                const auto vp = _camera->getViewport();
                const auto proj = _camera->projectionMatrix->transform();

                LOG_INFO("MCH-DBG: viewport x=%.1f y=%.1f w=%.1f h=%.1f | proj00=%.4f proj11=%.4f",
                         vp.x, vp.y, vp.width, vp.height, proj(0, 0), proj(1, 1));
            }

            for (const auto &hit : intersector->intersections)
            {
                LOG_INFO("MCH-DBG:   ratio=%.3f world=(%.3f %.3f %.3f)", hit->ratio,
                         hit->worldIntersection.x, hit->worldIntersection.y, hit->worldIntersection.z);
            }
        }

        for (const auto &hit : intersector->intersections)
        {
            std::string hit_path = "";
            for (auto node_it = hit->nodePath.rbegin(); node_it != hit->nodePath.rend(); ++node_it)
            {
                std::string node_name = "";
                (*node_it)->getValue("name", node_name);
                if (node_name.empty())
                {
                    (*node_it)->getValue("Name", node_name);
                }
                if (!node_name.empty())
                {
                    if (!hit_path.empty()) hit_path += "/";
                    hit_path += node_name;
                }
            }
            if (mch_debug())
            {
                LOG_INFO("MCH-DBG:   hit path='%s'", hit_path.c_str());
            }

            for (auto node_it = hit->nodePath.rbegin(); node_it != hit->nodePath.rend(); ++node_it)
            {
                std::string node_name = "";
                (*node_it)->getValue("name", node_name);

                if (node_name.empty())
                {
                    (*node_it)->getValue("Name", node_name);
                }

                if (node_name.empty())
                {
                    continue;
                }

                for (auto *io_controller : vehicle->io_controls)
                {
                    if (io_controller == nullptr)
                    {
                        continue;
                    }

                    io_controller->findControl(node_name, input);

                    if (input.id == 0)
                    {
                        continue;
                    }

                    // Сохраняем актуальный контроллер ввода
                    io_ctrl = io_controller;

                    if (_last_hit_object != node_name)
                    {
                        _last_hit_object = node_name;
                        LOG_INFO("Clicked: %s state: %3.1f", node_name.c_str(), input.value);
                    }

                    return true;
                }
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ControlTooltip &getControlTooltip()
{
    static ControlTooltip tooltip;
    return tooltip;
}
