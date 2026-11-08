# L6 — networking, next to physics: it will replicate gameplay objects and components, and gameplay
# never names it. The sockets, packets and dispatcher themselves are PluNetwork, a library of its own.
plu_add_engine_module(PluNetworking DEPENDS PluCore PluGameplay)

target_link_libraries(PluNetworking PUBLIC PluNetwork::PluNetwork)
