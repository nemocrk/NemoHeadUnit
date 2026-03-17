#include <pybind11/pybind11.h>

#include <aasdk/Common/ModernLogger.hpp>
#include <app/core/logging.hpp>

namespace py = pybind11;

namespace
{

    aasdk::common::LogLevel parse_level(const py::handle &level)
    {
        if (py::isinstance<py::int_>(level))
        {
            return static_cast<aasdk::common::LogLevel>(py::cast<int>(level));
        }
        if (py::isinstance<py::str>(level))
        {
            const std::string s = py::cast<std::string>(level);
            if (s == "TRACE")
                return aasdk::common::LogLevel::TRACE;
            if (s == "DEBUG")
                return aasdk::common::LogLevel::DEBUG;
            if (s == "INFO")
                return aasdk::common::LogLevel::INFO;
            if (s == "WARN")
                return aasdk::common::LogLevel::WARN;
            if (s == "ERROR")
                return aasdk::common::LogLevel::ERROR;
            if (s == "FATAL")
                return aasdk::common::LogLevel::FATAL;
            throw std::invalid_argument("Unknown log level string: " + s);
        }
        throw std::invalid_argument("level must be int or str");
    }

    py::object g_cpp_log_handler = py::none();
    py::object g_cpp_should_log_handler = py::none();

    void python_log_trampoline(const std::string &component, int level, const std::string &message)
    {
        py::gil_scoped_acquire gil;
        try
        {
            if (!g_cpp_log_handler.is_none())
            {
                g_cpp_log_handler(component, level, message);
            }
        }
        catch (...)
        {
            // Swallow exceptions to avoid crashing C++.
        }
    }

    bool python_should_log_trampoline(const std::string &component, int level)
    {
        if (g_cpp_should_log_handler.is_none())
            return true;

        py::gil_scoped_acquire gil;
        try
        {
            return py::cast<bool>(g_cpp_should_log_handler(component, level));
        }
        catch (...)
        {
            return true;
        }
    }

    aasdk::common::LogCategory parse_category(const py::handle &category)
    {
        if (py::isinstance<py::int_>(category))
        {
            return static_cast<aasdk::common::LogCategory>(py::cast<int>(category));
        }
        if (py::isinstance<py::str>(category))
        {
            const std::string s = py::cast<std::string>(category);
            if (s == "SYSTEM")
                return aasdk::common::LogCategory::SYSTEM;
            if (s == "TRANSPORT")
                return aasdk::common::LogCategory::TRANSPORT;
            if (s == "CHANNEL")
                return aasdk::common::LogCategory::CHANNEL;
            if (s == "USB")
                return aasdk::common::LogCategory::USB;
            if (s == "TCP")
                return aasdk::common::LogCategory::TCP;
            if (s == "MESSENGER")
                return aasdk::common::LogCategory::MESSENGER;
            if (s == "PROTOCOL")
                return aasdk::common::LogCategory::PROTOCOL;
            if (s == "AUDIO")
                return aasdk::common::LogCategory::AUDIO;
            if (s == "VIDEO")
                return aasdk::common::LogCategory::VIDEO;
            if (s == "INPUT")
                return aasdk::common::LogCategory::INPUT;
            if (s == "SENSOR")
                return aasdk::common::LogCategory::SENSOR;
            if (s == "BLUETOOTH")
                return aasdk::common::LogCategory::BLUETOOTH;
            if (s == "WIFI")
                return aasdk::common::LogCategory::WIFI;
            if (s == "GENERAL")
                return aasdk::common::LogCategory::GENERAL;
            throw std::invalid_argument("Unknown log category string: " + s);
        }
        throw std::invalid_argument("category must be int or str");
    }

} // namespace

void init_logger_bindings(py::module_ &m)
{
    py::enum_<aasdk::common::LogLevel>(m, "LogLevel")
        .value("TRACE", aasdk::common::LogLevel::TRACE)
        .value("DEBUG", aasdk::common::LogLevel::DEBUG)
        .value("INFO", aasdk::common::LogLevel::INFO)
        .value("WARN", aasdk::common::LogLevel::WARN)
        .value("ERROR", aasdk::common::LogLevel::ERROR)
        .value("FATAL", aasdk::common::LogLevel::FATAL);

    py::enum_<aasdk::common::LogCategory>(m, "LogCategory")
        .value("SYSTEM", aasdk::common::LogCategory::SYSTEM)
        .value("TRANSPORT", aasdk::common::LogCategory::TRANSPORT)
        .value("CHANNEL", aasdk::common::LogCategory::CHANNEL)
        .value("USB", aasdk::common::LogCategory::USB)
        .value("TCP", aasdk::common::LogCategory::TCP)
        .value("MESSENGER", aasdk::common::LogCategory::MESSENGER)
        .value("PROTOCOL", aasdk::common::LogCategory::PROTOCOL)
        .value("AUDIO", aasdk::common::LogCategory::AUDIO)
        .value("VIDEO", aasdk::common::LogCategory::VIDEO)
        .value("INPUT", aasdk::common::LogCategory::INPUT)
        .value("SENSOR", aasdk::common::LogCategory::SENSOR)
        .value("BLUETOOTH", aasdk::common::LogCategory::BLUETOOTH)
        .value("WIFI", aasdk::common::LogCategory::WIFI)
        .value("GENERAL", aasdk::common::LogCategory::GENERAL)
        .value("CHANNEL_CONTROL", aasdk::common::LogCategory::CHANNEL_CONTROL)
        .value("CHANNEL_BLUETOOTH", aasdk::common::LogCategory::CHANNEL_BLUETOOTH)
        .value("CHANNEL_MEDIA_SINK", aasdk::common::LogCategory::CHANNEL_MEDIA_SINK)
        .value("CHANNEL_MEDIA_SOURCE", aasdk::common::LogCategory::CHANNEL_MEDIA_SOURCE)
        .value("CHANNEL_INPUT_SOURCE", aasdk::common::LogCategory::CHANNEL_INPUT_SOURCE)
        .value("CHANNEL_SENSOR_SOURCE", aasdk::common::LogCategory::CHANNEL_SENSOR_SOURCE)
        .value("CHANNEL_NAVIGATION", aasdk::common::LogCategory::CHANNEL_NAVIGATION)
        .value("CHANNEL_PHONE_STATUS", aasdk::common::LogCategory::CHANNEL_PHONE_STATUS)
        .value("CHANNEL_RADIO", aasdk::common::LogCategory::CHANNEL_RADIO)
        .value("CHANNEL_NOTIFICATION", aasdk::common::LogCategory::CHANNEL_NOTIFICATION)
        .value("CHANNEL_VENDOR_EXT", aasdk::common::LogCategory::CHANNEL_VENDOR_EXT)
        .value("CHANNEL_WIFI_PROJECTION", aasdk::common::LogCategory::CHANNEL_WIFI_PROJECTION)
        .value("CHANNEL_MEDIA_BROWSER", aasdk::common::LogCategory::CHANNEL_MEDIA_BROWSER)
        .value("CHANNEL_PLAYBACK_STATUS", aasdk::common::LogCategory::CHANNEL_PLAYBACK_STATUS)
        .value("AUDIO_GUIDANCE", aasdk::common::LogCategory::AUDIO_GUIDANCE)
        .value("AUDIO_MEDIA", aasdk::common::LogCategory::AUDIO_MEDIA)
        .value("AUDIO_SYSTEM", aasdk::common::LogCategory::AUDIO_SYSTEM)
        .value("AUDIO_TELEPHONY", aasdk::common::LogCategory::AUDIO_TELEPHONY)
        .value("AUDIO_MICROPHONE", aasdk::common::LogCategory::AUDIO_MICROPHONE)
        .value("VIDEO_SINK", aasdk::common::LogCategory::VIDEO_SINK)
        .value("VIDEO_CHANNEL", aasdk::common::LogCategory::VIDEO_CHANNEL);

    py::class_<aasdk::common::ModernLogger>(m, "ModernLogger")
        .def_static(
            "instance",
            []() -> aasdk::common::ModernLogger &
            {
                return aasdk::common::ModernLogger::getInstance();
            },
            py::return_value_policy::reference)
        .def("set_level",
             [](aasdk::common::ModernLogger &self, py::object level)
             {
                 self.setLevel(parse_level(level));
             })
        .def("set_category_level",
             [](aasdk::common::ModernLogger &self, py::object category, py::object level)
             {
                 self.setCategoryLevel(parse_category(category), parse_level(level));
             })
        .def("should_log", &aasdk::common::ModernLogger::shouldLog)
        .def("flush", &aasdk::common::ModernLogger::flush)
        .def("shutdown", &aasdk::common::ModernLogger::shutdown)
        .def("get_queue_size", &aasdk::common::ModernLogger::getQueueSize)
        .def("get_dropped_messages", &aasdk::common::ModernLogger::getDroppedMessages)
        .def_static("level_to_string", &aasdk::common::ModernLogger::levelToString)
        .def_static("category_to_string", &aasdk::common::ModernLogger::categoryToString)
        .def_static("string_to_level", &aasdk::common::ModernLogger::stringToLevel)
        .def_static("string_to_category", &aasdk::common::ModernLogger::stringToCategory);

    m.def(
        "set_aasdk_log_level",
        [](py::object level)
        {
            auto &logger = aasdk::common::ModernLogger::getInstance();
            logger.setLevel(parse_level(level));
        },
        py::arg("level"),
        "Imposta il livello di log globale di aasdk::ModernLogger.\n"
        "level: str tra {'TRACE','DEBUG','INFO','WARN','ERROR','FATAL'} oppure int enum.");

    m.def(
        "set_aasdk_category_log_level",
        [](py::object category, py::object level)
        {
            auto &logger = aasdk::common::ModernLogger::getInstance();
            logger.setCategoryLevel(parse_category(category), parse_level(level));
        },
        py::arg("category"),
        py::arg("level"),
        "Imposta il livello di log per una categoria.\n"
        "category: str (es. 'USB','TCP','VIDEO','AUDIO','SYSTEM',...) oppure int enum.");

    m.def(
        "configure_aasdk_logging",
        [](py::object global_level,
           py::object usb_level,
           py::object tcp_level)
        {
            auto &logger = aasdk::common::ModernLogger::getInstance();
            logger.setLevel(parse_level(global_level));
            logger.setCategoryLevel(aasdk::common::LogCategory::USB, parse_level(usb_level));
            logger.setCategoryLevel(aasdk::common::LogCategory::TCP, parse_level(tcp_level));
        },
        py::arg("global_level") = py::str("TRACE"),
        py::arg("usb_level") = py::str("DEBUG"),
        py::arg("tcp_level") = py::str("DEBUG"),
        "Shortcut: configura global + USB + TCP (valori default come la vecchia enable_aasdk_logging()).");

    m.def(
        "set_cpp_log_handler",
        [](py::object handler)
        {
            if (handler.is_none())
            {
                g_cpp_log_handler = py::none();
                nemo::g_python_log_fn = nullptr;
                return;
            }
            if (!py::isinstance<py::function>(handler))
            {
                throw std::invalid_argument("handler must be callable");
            }
            g_cpp_log_handler = handler;
            nemo::g_python_log_fn = python_log_trampoline;
        },
        py::arg("handler"),
        "Register a Python callable to receive log messages from C++.");

    m.def(
        "set_cpp_should_log_handler",
        [](py::object handler)
        {
            if (handler.is_none())
            {
                g_cpp_should_log_handler = py::none();
                nemo::g_python_should_log_fn = nullptr;
                return;
            }
            if (!py::isinstance<py::function>(handler))
            {
                throw std::invalid_argument("handler must be callable");
            }
            g_cpp_should_log_handler = handler;
            nemo::g_python_should_log_fn = python_should_log_trampoline;
        },
        py::arg("handler"),
        "Register a Python callable to decide whether a C++ log should be emitted (should return bool). It is called with (component, level).");
}

PYBIND11_MODULE(aasdk_logging, m)
{
    init_logger_bindings(m);
}
