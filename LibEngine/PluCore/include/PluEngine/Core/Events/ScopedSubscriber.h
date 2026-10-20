//
// Created by Plutex on 2026-01-24.
//

#ifndef PLUENGINE_SCOPEDSUBSCRIBER_H
#define PLUENGINE_SCOPEDSUBSCRIBER_H

#include "EventDispatcher.h"
#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	class PLUCORE_API ScopedSubscriber {
	public:
		ScopedSubscriber() = default;
		ScopedSubscriber(EventDispatcher* dispatcher, const String& eventName, EventCallback callback);

		ScopedSubscriber(const ScopedSubscriber&) = delete;
		ScopedSubscriber& operator=(const ScopedSubscriber&) = delete;

		ScopedSubscriber(ScopedSubscriber&& other) noexcept;
		ScopedSubscriber& operator=(ScopedSubscriber&& other) noexcept;

		~ScopedSubscriber();

		void Disconnect();

	private:
		EventDispatcher* m_Dispatcher = nullptr;
		String m_EventName;
		EventHandle m_Handle = 0;
	};
}

#endif //PLUENGINE_SCOPEDSUBSCRIBER_H