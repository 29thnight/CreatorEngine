export module Creator.Core.Progress;

import std;

export namespace ce::progress
{
    struct sink
    {
        std::function<void()> launch;
        std::function<void(const std::wstring&)> set_title;
        std::function<void(const std::wstring&)> set_status;
        std::function<void(float)> set_progress;
        std::function<void()> close;
    };

    // The implementation unit owns one registry per linked runtime image.
    sink& get_sink();

    inline void launch()
    {
        if (get_sink().launch)
        {
            get_sink().launch();
        }
    }
    inline void set_title(const std::wstring& text)
    {
        if (get_sink().set_title)
        {
            get_sink().set_title(text);
        }
    }
    inline void set_status(const std::wstring& text)
    {
        if (get_sink().set_status)
        {
            get_sink().set_status(text);
        }
    }
    inline void set_progress(float percent)
    {
        if (get_sink().set_progress)
        {
            get_sink().set_progress(percent);
        }
    }
    inline void close()
    {
        if (get_sink().close)
        {
            get_sink().close();
        }
    }
} // namespace ce::progress
