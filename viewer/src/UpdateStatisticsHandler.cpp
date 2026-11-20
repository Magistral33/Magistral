#include "UpdateStatisticsHandler.h"
#include "vsg/ui/ApplicationEvent.h"

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
UpdateStatisticsHandler::UpdateStatisticsHandler()
{
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    std::fill(frames_timepoints.begin(), frames_timepoints.end(), t);
    std::fill(frames_FPS.begin(), frames_FPS.end(), 0.0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void UpdateStatisticsHandler::apply(vsg::FrameEvent& frame)
{
    uint8_t prev_index = frame_index;
    ++frame_index;
    if (frame_index >= FRAME_ARRAY_SIZE)
        frame_index = 0;

    using seconds = std::chrono::duration<double, std::chrono::seconds::period>;

    const auto now = std::chrono::steady_clock::now();

    // Время кадра берём из реальных часов, а не из метки кадра: при паузе
    // (окно свёрнуто, загрузка идёт) метка кадра отстаёт, и счётчик выдаёт
    // фантастические значения вместо честного нуля
    const double frames_duration = seconds(now - frames_timepoints[frame_index]).count();
    const double last_frame_duration = seconds(now - frames_timepoints[prev_index]).count();

    frames_timepoints[frame_index] = now;

    if (frames_duration < 1e-10 || last_frame_duration < 1e-10)
        return;

    // Только реальные кадры: если с прошлого замера прошло больше 0,5 с,
    // значит, кадры не подавались, и среднее по старому окну недействительно
    const double frames_count = static_cast<double>(filled_frames);
    if (frames_count > 0 && frames_duration > 1e-6 && frames_count / frames_duration < 20000.0)
    {
        average_FPS = frames_count / frames_duration;
    }

    frames_FPS[frame_index] = 1.0 / last_frame_duration;
    lowest_FPS = *std::min_element(frames_FPS.begin(), frames_FPS.end());

    if (filled_frames < FRAME_ARRAY_SIZE)
        ++filled_frames;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double UpdateStatisticsHandler::getAverageFPS() const noexcept
{
    return average_FPS;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double UpdateStatisticsHandler::getLowestFPS() const noexcept
{
    return lowest_FPS;
}
