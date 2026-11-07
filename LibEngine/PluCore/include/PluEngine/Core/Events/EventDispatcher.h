//
// Created by Plutex on 2026-01-24.
//

#ifndef PLUENGINE_EVENTDISPATCHER_H
#define PLUENGINE_EVENTDISPATCHER_H

#include <functional>
#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	using EventCallback = std::function<void(void*)>;
	using EventHandle = Int32;


	class PLUCORE_API EventDispatcher {
	public:
		struct Subscription {
			EventHandle Handle;
			EventCallback Callback;
		};

		EventDispatcher() = default;
		~EventDispatcher() = default;

		EventHandle Subscribe(const String& eventName, EventCallback callback);
		void Unsubscribe(const String& eventName, EventHandle handle);
		void Dispatch(const String& eventName, void* data = nullptr);

	private:
		HashMap<String, DynamicArray<Subscription>> m_Subscribers;
		inline static EventHandle s_NextHandle = 1;

		friend class ScopedSubscriber;
	};
}

#endif //PLUENGINE_EVENTDISPATCHER_H