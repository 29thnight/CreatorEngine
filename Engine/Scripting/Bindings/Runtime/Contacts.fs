namespace CreatorEngine

open System
open System.Collections.Generic
open CreatorEngine

/// Indexed routing and subscription ownership. Span storage and native ABI value
/// layouts stay in the binding assembly; no script references survive teardown.
type internal Contacts(isUpdating: unit -> bool) =
    let roles = Dictionary<struct (ObjectHandle * uint64 * uint32), ShapeRole>()

    let routes =
        Dictionary<struct (ObjectHandle * ShapeRole * ShapeRole * ContactPhases), ResizeArray<ContactStream>>()

    let touched = ResizeArray<ContactStream>()

    let subscribers =
        Dictionary<Component, ResizeArray<ContactStream>>(HashIdentity.Reference)

    let phases = [| ContactPhases.Begin; ContactPhases.Persist; ContactPhases.End |]
    let mutable current: Component = null

    member private _.ResolveRole(endpoint: ContactEndpoint) =
        if not (endpoint.Role.Equals(Guid.Empty)) then
            ShapeRole(endpoint.Role)
        else
            let mutable role = Unchecked.defaultof<ShapeRole>
            let key = struct (endpoint.Owner, endpoint.Component, endpoint.Shape)

            if roles.TryGetValue(key, &role) then
                role
            else
                Unchecked.defaultof<ShapeRole>

    member private _.Deliver(key, value: NativeContact, reverse, phase) =
        let mutable streams = Unchecked.defaultof<ResizeArray<ContactStream>>

        if routes.TryGetValue(key, &streams) then
            let mutable index = 0

            while index < streams.Count do
                let stream = streams[index]

                if
                    not stream.Disposed
                    && stream.Subscriber.Enabled
                    && stream.Subscriber.BeginSucceeded
                then
                    let observed = stream.ObservePhase(&value, phase, reverse)

                    if int observed <> 0 || stream.Overflowed then
                        if not stream.Touched then
                            touched.Add(stream)
                            stream.Touched <- true

                        if int observed <> 0 then
                            let contact = Contact(&value, reverse, observed)
                            stream.Append(&contact)

                index <- index + 1

    interface IContactDispatcher with
        member _.Current
            with get () = current
            and set value = current <- value

        member _.Suspend(subscriber) =
            match subscribers.TryGetValue(subscriber) with
            | true, streams ->
                for stream in streams do
                    touched.Remove(stream) |> ignore
                    stream.ResetObservation()
            | _ -> ()

        member _.BindRole(owner, body, shape, role, lifetime) =
            let key = struct (owner, body, shape)

            if not (roles.TryAdd(key, role)) then
                invalidOp "Shape already has a contact role binding."

            lifetime.RegisterCleanup(Action(fun () -> roles.Remove(key) |> ignore))

        member this.Observe(subscriber, owner, self, other, requestedPhases, capacity, grouping) =
            if
                not owner.IsValid
                || self.Id = Guid.Empty
                || other.Id = Guid.Empty
                || requestedPhases = enum<ContactPhases>(0)
                || (requestedPhases &&& ~~~ContactPhases.All) <> enum<ContactPhases>(0)
                || capacity <= 0
                || capacity > 65536
                || not (Enum.IsDefined(grouping))
            then
                invalidArg "subscription" "Invalid contact subscription."

            let stream =
                new ContactStream(subscriber, owner, self, other, requestedPhases, capacity, grouping, isUpdating, fun stream -> (this :> IContactDispatcher).Remove(stream))

            // Track every sensor transition even for Begin-only subscribers.
            for phase in phases do
                let key = struct (owner, self, other, phase)

                let streams =
                    match routes.TryGetValue(key) with
                    | true, streams -> streams
                    | _ ->
                        let streams = ResizeArray<ContactStream>()
                        routes.Add(key, streams)
                        streams

                streams.Add(stream)

            let owned =
                match subscribers.TryGetValue(subscriber) with
                | true, owned -> owned
                | _ ->
                    let owned = ResizeArray<ContactStream>()
                    subscribers.Add(subscriber, owned)
                    owned

            owned.Add(stream)
            subscriber.Lifetime.RegisterCleanup(Action(stream.Dispose))
            stream

        member _.Remove(stream) =
            touched.Remove(stream) |> ignore

            match subscribers.TryGetValue(stream.Subscriber) with
            | true, owned ->
                owned.Remove(stream) |> ignore

                if owned.Count = 0 then
                    subscribers.Remove(stream.Subscriber) |> ignore
            | _ -> ()

            for phase in phases do
                let key = struct (stream.Owner, stream.SelfRole, stream.OtherRole, phase)

                match routes.TryGetValue(key) with
                | true, streams ->
                    streams.Remove(stream) |> ignore

                    if streams.Count = 0 then
                        routes.Remove(key) |> ignore
                | _ -> ()

        member this.Route(value) =
            let phase =
                match value.Kind with
                | 0
                | 3 -> ContactPhases.Begin
                | 1
                | 5 -> ContactPhases.Persist
                | 2
                | 4 -> ContactPhases.End
                | _ -> invalidArg "value" "Unknown contact phase."

            let first = this.ResolveRole(value.First)
            let second = this.ResolveRole(value.Second)

            if not (first.Id.Equals(Guid.Empty)) && not (second.Id.Equals(Guid.Empty)) then
                this.Deliver(struct (value.First.Owner, first, second, phase), value, false, phase)
                this.Deliver(struct (value.Second.Owner, second, first, phase), value, true, phase)

        member _.EndUpdate() =
            let mutable iterator = subscribers.GetEnumerator()

            while iterator.MoveNext() do
                let streams = iterator.Current.Value

                for index in 0 .. streams.Count - 1 do
                    streams[index].ClearTick()

            iterator.Dispose()

        member _.EndFrame() =
            for index in 0 .. touched.Count - 1 do
                touched[index].Clear()

            touched.Clear()
