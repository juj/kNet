/* Copyright 2010 Jukka Jylänki

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */
#pragma once

/** @file kNet.h
	@brief The main file of KristalliNet to #include by the client application. */

#include "kNet/Network.h"
#include "kNet/NetworkServer.h"
#include "kNet/MessageConnection.h"
#include "kNet/IMessageHandler.h"
#include "kNet/DataSerializer.h"
#include "kNet/DataDeserializer.h"
#include "kNet/NetException.h"
