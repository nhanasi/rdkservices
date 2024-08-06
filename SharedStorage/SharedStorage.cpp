/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2022 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SharedStorage.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {

namespace {

    static Plugin::Metadata<Plugin::SharedStorage> metadata(
        // Version (Major, Minor, Patch)
        API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
        // Preconditions
        {},
        // Terminations
        {},
        // Controls
        {});
}

namespace Plugin {
    using namespace JsonData::PersistentStore;

    SERVICE_REGISTRATION(SharedStorage, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    SharedStorage::SharedStorage()
        : PluginHost::JSONRPC()
        , _service(nullptr)
        , _psEngine(Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create())
        , _psCommunicatorClient(Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(_psEngine)))
        , _csEngine(Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create())
        , _csCommunicatorClient(Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(_csEngine)))
        , _psController(nullptr)
        , _csController(nullptr)
        , _psObject(nullptr)
        , _psCache(nullptr)
        , _psInspector(nullptr)
        , _psLimit(nullptr)
        , _csObject(nullptr)
        , _storeNotification(*this)
    {
        RegisterAll();
    }
    SharedStorage::~SharedStorage()
    {
        UnregisterAll();
    }

    Exchange::IStore2* SharedStorage::getRemoteStoreObject(ScopeType eScope)
    {
        if( (eScope == ScopeType::DEVICE) && _psObject)
        {
            return _psObject;
        }
        else if( (eScope == ScopeType::ACCOUNT) && _csObject)
        {
            return _csObject;
        }
        else
        {
            return nullptr;
        }
    }

    const string SharedStorage::Initialize(PluginHost::IShell* service)
    {
        SYSLOG(Logging::Startup, (_T("SharedStorage::Initialize: PID=%u"), getpid()));
        string message;

        ASSERT(service != nullptr);
        ASSERT(nullptr == _service);

        _service = service;
        _service->AddRef();

        if (!_psCommunicatorClient.IsValid())
        {
            TRACE(Trace::Error, (_T("%s Invalid _psCommunicatorClient"), __FUNCTION__));
            message = _T("SharedStorage plugin could not be initialized.");
        }
        else
        {
            #if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
                _psEngine->Announcements(_psCommunicatorClient->Announcement());
            #endif
            _psController = _psCommunicatorClient->Open<PluginHost::IShell>("org.rdk.PersistentStore", ~0, 3000);
            if (_psController)
            {
                // Get interface for IStore2
                _psObject = _psController->QueryInterface<Exchange::IStore2>();
                if(_psObject)
                {
                    _psObject->Register(&_storeNotification);
                }
                else
                {
                    TRACE(Trace::Error, (_T("%s Connect fail to _psObject"), __FUNCTION__));
                    message = _T("SharedStorage plugin could not be initialized.");
                }

                // Get interface for IStoreInspector
                _psInspector = _psController->QueryInterface<Exchange::IStoreInspector>();
                if(!_psInspector)
                {
                    TRACE(Trace::Error, (_T("%s Connect fail to _psInspector"), __FUNCTION__));
                    message = _T("SharedStorage plugin could not be initialized.");
                }

                // Get interface for IStoreLimit
                _psLimit = _psController->QueryInterface<Exchange::IStoreLimit>();
                if(!_psLimit)
                {
                    TRACE(Trace::Error, (_T("%s Connect fail to _psLimit"), __FUNCTION__));
                    message = _T("SharedStorage plugin could not be initialized.");
                }

                // Get interface for IStoreCache
                _psCache = _psController->QueryInterface<Exchange::IStoreCache>();
                if(!_psCache)
                {
                    TRACE(Trace::Error, (_T("%s Connect fail to _psCache"), __FUNCTION__));
                    message = _T("SharedStorage plugin could not be initialized.");
                }
            }
        }

        // Establish communication with CloudStore
        if (!_csCommunicatorClient.IsValid())
        {
            TRACE(Trace::Error, (_T("%s Invalid _csCommunicatorClient"), __FUNCTION__));
            message = _T("SharedStorage plugin could not be initialized.");
        }
        else
        {
            #if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
                _csEngine->Announcements(_csCommunicatorClient->Announcement());
            #endif
            _csController = _csCommunicatorClient->Open<PluginHost::IShell>("org.rdk.CloudStore", ~0, 3000);
            if (_csController)
            {
                _csObject = _csController->QueryInterface<Exchange::IStore2>();
                if(_csObject)
                {
                    _csObject->Register(&_storeNotification);
                }
                else
                {
                    TRACE(Trace::Error, (_T("%s Connect fail to _csObject"), __FUNCTION__));
                    message = _T("SharedStorage plugin could not be initialized.");
                }
            }
        }

        if (message.length() != 0) {
            Deinitialize(service);
        }
        SYSLOG(Logging::Shutdown, (string(_T("SharedStorage Initialize complete"))));
        return message;
    }

    void SharedStorage::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(_service == service);
        SYSLOG(Logging::Shutdown, (string(_T("SharedStorage::Deinitialize"))));

        // Disconnect from the COM-RPC socket
        if (_psController)
        {
            _psController->Release();
            _psController = nullptr;
        }
        if (_csController)
        {
            _csController->Release();
            _csController = nullptr;
        }
        _psCommunicatorClient->Close(RPC::CommunicationTimeOut);
        if (_psCommunicatorClient.IsValid())
        {
            _psCommunicatorClient.Release();
        }
        if(_psEngine.IsValid())
        {
            _psEngine.Release();
        }
        if(_psObject)
        {
            _psObject->Unregister(&_storeNotification);
            _psObject->Release();
        }
        if(_psInspector)
        {
            _psInspector->Release();
        }
        if(_psLimit)
        {
            _psLimit->Release();
        }
        _csCommunicatorClient->Close(RPC::CommunicationTimeOut);
        if (_csCommunicatorClient.IsValid())
        {
            _csCommunicatorClient.Release();
        }
        if(_csEngine.IsValid())
        {
            _csEngine.Release();
        }
        if(_psCache)
        {
            _psCache->Release();
        }
        if(_csObject)
        {
            _csObject->Unregister(&_storeNotification);
            _csObject->Release();
        }

        _service->Release();
        _service = nullptr;
        SYSLOG(Logging::Shutdown, (string(_T("SharedStorage Deinitialize complete"))));
    }

    string SharedStorage::Information() const
    {
        return (string());
    }
} // namespace Plugin
} // namespace WPEFramework
