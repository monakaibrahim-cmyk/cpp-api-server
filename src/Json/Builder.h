#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include <string>
#include <vector>
#include <sstream>

namespace Json
{
    class Builder
    {
        std::stringstream _stream;
        bool _first = true;

    public:
        Builder()
        {
            _stream << "{";
        }

        void add(std::string key, std::string value)
        {
            if (!_first)
            {
                _stream << ",";
            }

            _stream << "\"" << key << "\":\"" << value << "\"";
            _first = false;
        }

        void add(std::string key, int value)
        {
            if (!_first)
            {
                _stream << ",";
            }

            _stream << "\"" << key << "\":\"" << value << "\"";
            _first = false;
        }

        std::string build()
        {
            _stream << "}";

            return _stream.str();
        }
    };
}

#endif // JSON_BUILDER_H