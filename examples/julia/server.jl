# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# server.jl - IEC 104 server (controlled station) in Julia.
#
# Station 1 with a measured value at IOA 100 (short float). The value rises
# every second and is sent spontaneously; the library answers a general
# interrogation by itself. Commands are printed and accepted.
#
#   julia server.jl [port] [seconds] [address]
#     port      default 2404, 0 = any free port
#     seconds   run time, default 0 = until Ctrl-C
#     address   default 127.0.0.1 (this computer only); 0.0.0.0 = all
#               addresses, only deliberately (the station accepts commands)

include(joinpath(@__DIR__, "IEC104.jl"))
using .IEC104

mutable struct ServerState
    commands::Vector{Tuple{UInt32,AsduInfo,Object}}
end

# Runs inside server_poll(), called from C: decide and store, print later.
function on_command(user::Ptr{Cvoid}, conn::UInt32, info::Ptr{AsduInfo}, cmd::Ptr{Object})::Cint
    s = unsafe_pointer_to_objref(user)::ServerState
    push!(s.commands, (conn, unsafe_load(info), unsafe_load(cmd)))
    return Cint(1)   # 1 = accept (positive confirmation), 0 = reject
end

function main(port = IEC104.DEFAULT_PORT, seconds = 0, address = "127.0.0.1")
    st = ServerState(Tuple{UInt32,AsduInfo,Object}[])
    cb = Callbacks(st;
        on_command = @cfunction(on_command, Cint, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})))
    GC.@preserve st begin
        server = server_create(LinkConfig(), cb)
        try
            value = 0.0
            server_add_point(server, 1, Object(13, 100, value))   # M_ME_NC_1, group 1
            server_listen(server, port; address = address)
            println("listening on port ", server_port(server))
            flush(stdout)   # stdout to a file is buffered; scripts wait for this line
            t_end = seconds > 0 ? time() + seconds : Inf
            next = time() + 1
            while time() < t_end
                server_poll(server, 100)
                for (conn, i, c) in st.commands
                    println("connection $conn: command type $(i.type), IOA $(c.ioa), value $(c.value)")
                end
                empty!(st.commands)
                flush(stdout)
                if time() >= next                                # once per second
                    value += 1
                    server_update(server, 1, Object(13, 100, value))
                    next += 1
                end
            end
        finally
            server_destroy(server)
        end
    end
end

if abspath(PROGRAM_FILE) == @__FILE__
    port = parse(Int, get(ARGS, 1, string(IEC104.DEFAULT_PORT)))
    seconds = parse(Int, get(ARGS, 2, "0"))
    main(port, seconds, get(ARGS, 3, "127.0.0.1"))
end
