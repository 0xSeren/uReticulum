#pragma once

#include <memory>
#include <string>

#include "ureticulum/bytes.h"
#include "ureticulum/type.h"

namespace RNS {

    class Interface;

    class InterfaceImpl : public std::enable_shared_from_this<InterfaceImpl> {
    public:
        virtual ~InterfaceImpl() = default;

        virtual bool  start()        { return true; }
        virtual void  stop()         {}
        virtual void  loop()         {}

        virtual void  send_outgoing(const Bytes& data) = 0;
        void          handle_incoming(const Bytes& data);

        virtual std::string toString() const { return "Interface[" + _name + "]"; }

    protected:
        InterfaceImpl() = default;
        explicit InterfaceImpl(const char* name) : _name(name) {}

        std::string _name;
        bool        _online = false;
        size_t      _rxb    = 0;
        size_t      _txb    = 0;

        friend class Interface;
    };

    class Interface {
    public:
        Interface(Type::NoneConstructor) {}
        Interface(const Interface& other) : _impl(other._impl) {}
        Interface(std::shared_ptr<InterfaceImpl> impl) : _impl(std::move(impl)) {}

        Interface& operator=(const Interface& other) { _impl = other._impl; return *this; }
        explicit operator bool() const { return _impl != nullptr; }
        bool operator<(const Interface& other) const { return _impl.get() < other._impl.get(); }
        bool operator==(const Interface& other) const { return _impl.get() == other._impl.get(); }

        void send_outgoing(const Bytes& data) const { if (_impl) _impl->send_outgoing(data); }
        void handle_incoming(const Bytes& data) const { if (_impl) _impl->handle_incoming(data); }
        void loop() const { if (_impl) _impl->loop(); }

        std::string toString() const { return _impl ? _impl->toString() : ""; }

        InterfaceImpl* get() const { return _impl.get(); }

    private:
        std::shared_ptr<InterfaceImpl> _impl;
    };

}
