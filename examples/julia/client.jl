# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# client.jl - IEC 104 client (controlling station) in Julia.
#
# Connects, starts data transfer, sends a general interrogation to station 1,
# prints every object and ends after the interrogation and three spontaneous
# values.
#
#   julia client.jl [host] [port]

include(joinpath(@__DIR__, "IEC104.jl"))
using .IEC104

"What the callbacks collect (passed to C as `user`)."
mutable struct ClientState
    state::Cint
    reason::Cint
    received::Vector{Tuple{AsduInfo,Object}}
end

# Callbacks run inside client_poll(), called from C: they only store data.
# No println, no sleep, no task switch here; that would corrupt Julia's stack.
function on_state(user::Ptr{Cvoid}, ::UInt32, state::Cint, reason::Cint)::Cvoid
    s = unsafe_pointer_to_objref(user)::ClientState
    s.state = state
    s.reason = reason
    return nothing
end

function on_object(user::Ptr{Cvoid}, ::UInt32, info::Ptr{AsduInfo}, obj::Ptr{Object})::Cvoid
    s = unsafe_pointer_to_objref(user)::ClientState
    push!(s.received, (unsafe_load(info), unsafe_load(obj)))
    return nothing
end

function main(host = "127.0.0.1", port = IEC104.DEFAULT_PORT)
    st = ClientState(IEC104.LINK_CONNECTING, 0, Tuple{AsduInfo,Object}[])
    cb = Callbacks(st;
        on_state = @cfunction(on_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
        on_object = @cfunction(on_object, Cvoid, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})))
    config = LinkConfig()          # standard values; e.g. config.t1_ms = 250_000 for long timers
    GC.@preserve st begin          # st is reached from C through `user`
        client = client_create(config, cb)
        try
            client_connect(client, host, port)
            started = interrogated = done = false
            spontaneous = 0
            while st.state != IEC104.LINK_CLOSED && !(done && spontaneous >= 3)
                client_poll(client, 100)            # callbacks run inside
                for (i, o) in st.received           # now print what arrived
                    println("type $(i.type)  cause $(i.cause)  station $(i.common_address)  ",
                            "IOA $(o.ioa)  value $(o.value)")
                    i.type == 100 && i.cause == IEC104.COT_ACTIVATION_TERMINATION && (done = true)
                    i.cause == IEC104.COT_SPONTANEOUS && (spontaneous += 1)
                end
                empty!(st.received)
                if st.state == IEC104.LINK_STOPPED && !started
                    client_start(client)            # STARTDT
                    started = true
                elseif st.state == IEC104.LINK_STARTED && !interrogated
                    client_interrogation(client, 1) # station 1, all points
                    interrogated = true
                end
            end
            st.state == IEC104.LINK_CLOSED && println("connection closed, reason ", st.reason)
            return st.state != IEC104.LINK_CLOSED
        finally
            client_destroy(client)
        end
    end
end

if abspath(PROGRAM_FILE) == @__FILE__
    host = get(ARGS, 1, "127.0.0.1")
    port = parse(Int, get(ARGS, 2, string(IEC104.DEFAULT_PORT)))
    exit(main(host, port) ? 0 : 1)
end
