<!--
SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# Secure NMOS facade — plan

Status: proposed design

Scope: a new nmos-cpp program. It does not extend libnvnmos, nvnmosd, or gst-nmos-rs.

## 1. Goal

Give a hidden island of insecure Nodes a secure control plane without upgrading those Nodes.

The facade is an NMOS application-layer security gateway, implemented as a
protocol-aware reverse proxy.

One facade process watches the island's NMOS Registry. For each Node registered there it publishes a secure Node with the same id, the same Devices, and the same Sources, Flows, Senders, and Receivers. Secure clients use the facade. The island Registry and the island Nodes stay on the island network.

When a secure Registry is present, each secure Node registers itself and heartbeats itself. With no secure Registry, controllers find the facade Nodes through their own DNS-SD advertisements.

## 2. Topology

```text
Secure controllers and, when present, a secure Registry
        |  HTTPS and WSS, IS-10
        v
Facade process
        |  Query API WebSocket(s), held open
        v
Hidden island Registry
        |  HTTP or WS, only to serve a secure request
        v
That island Node's APIs
```

The facade is placed so it can reach the island Registry and the island Nodes, and so the secure network cannot. Hiding the island is a network placement: the facade does not suppress the island Nodes' own DNS-SD advertisements. Those advertisements, and the island Registry, must not be browsable from the secure side. A controller that can see both will find two copies of every id.

The facade is given the island Registry address by configuration. It does not discover that Registry from the secure network.

## 3. Identity

The mapping is 1:1.

| Island resource | Secure resource |
|-----------------|-----------------|
| Node | One secure Node, same `id` |
| Device | One Device, same `id`, same `node_id` |
| Source, Flow, Sender, Receiver | Same id, same parent id |

`urn:x-nmos:tag:grouphint/v1.0` is `<group-name>:<role>[:<scope>]`. Scope defaults to `device`; the other value is `node`. Controllers match the group name only inside that boundary, and the role must be unique there. Keeping the Node id and the Device id lets the facade copy these tags unchanged. Collapsing several island Nodes onto one secure Node, or several Devices onto one Device, would merge groups that were distinct on the island.

Clocks and the Node's `interfaces` array stay as registered. They describe the island host and the media path. Sender `interface_bindings` keep referring to those interface names.

Section 5 puts a spec-defined secure facade on the AMWA APIs nmos-cpp supports. Any other advertised service or control is left as advertised, removed, or given an opt-in basic secure proxy. The facade does not carry the media.

## 4. Where the model comes from

The island Registry is the source of the IS-04 model. The facade holds Query API WebSockets open and applies each snapshot and grain to the matching secure Node.

The IS-04 subscription model takes a `resource_path` for one type, so the facade opens one WebSocket each for `/nodes`, `/devices`, `/sources`, `/flows`, `/senders`, and `/receivers`. When the island Registry is nmos-cpp, or another Registry that accepts the same extension, configuration can instead post a single subscription with an empty `resource_path`. nmos-cpp treats that empty path as every resource type, and each grain names the type in `path` (`nodes/{id}` rather than `{id}`).

That covers Node, Device, Source, Flow, Sender, and Receiver resources, including clocks, interfaces, and tags. Absolute URLs in those resources are rewritten as in §5 before the facade serves them or registers them. The facade answers IS-04 Node API GETs from this model. It does not poll island Nodes.

A grain that adds a Node starts a secure Node. A delete grain, including Registry garbage-collection after a missed heartbeat, unregisters that secure Node and closes its listeners. A reconnect reconciles the local model against a fresh snapshot.

## 5. Which services are proxied

There are two classes of advertised API.

### 5.1 AMWA APIs nmos-cpp supports

Securing these is core functionality. BCP-003-01, IS-10, and BCP-003-02 already define TLS, the authorization scope, and the path rules. The facade publishes a secure href in place of the island href and applies those rules. Implementation is ordered: IS-04 and IS-05 first, then the rest, so that adoption of the later APIs is encouraged rather than left behind. Until a given API's facade exists, that `services` or `controls` entry is omitted, so the island href is not advertised in the meantime.

nmos-cpp advertises:

- IS-04 Node API, served from the Query model (§4)
- IS-05 Connection (`urn:x-nmos:control:sr-ctrl/`) and the Sender manifest (`manifest_href`, `urn:x-nmos:control:manifest-base/v1.0`)
- IS-07 Events (`urn:x-nmos:control:events/`)
- IS-08 Channel Mapping (`urn:x-nmos:control:cm-ctrl/`)
- IS-12 (`urn:x-nmos:control:ncp/`)
- IS-13 Annotation, a Node service (`urn:x-nmos:service:annotation/` in `services`)
- IS-14 (`urn:x-nmos:control:configuration/`)

A secure request is forwarded only when it arrives, to the island Node identified by the resource id. An immediate IS-05 activation stays open across the forward and completes with the island Node's result. The transport file's media addresses stay as the island device registered them. A successful IS-13 PATCH updates label, description, and tags in the facade's model, because those fields are part of the IS-04 resources it serves and registers.

For each of these APIs the facade rewrites island URLs in three places, using the same island-base to facade-base mapping:

- **IS-04 resources.** The Node, Device, Source, Flow, Sender, and Receiver resources served by the facade's Node API, and the same resources it registers with the secure Registry. That includes `api.endpoints`, each `services` and `controls` href, Sender `manifest_href`, and any other absolute URL in the resource that addresses an island API.
- **Response bodies.** JSON from the proxied API. The transport-file body is the exception: its media addresses stay as registered.
- **`Location` headers.** Any proxied response that carries one, whether a redirect or a response that names a created or related resource. A `Link` header on that response is rewritten in the same pass.

### 5.2 Other advertised services and controls

Anything else the island Node puts in `services` or `controls` has no AMWA specification for how to authorize it. Configuration chooses one of three policies, per type URN, with an optional per-Node or per-Device override:

| Policy | Secure model | Behaviour |
|--------|--------------|-----------|
| `expose` | Keep the original href exactly as advertised | The controller contacts the island API directly. It may be HTTP and unauthenticated. This is an explicit exception to the secure boundary |
| `remove` | Omit the service or control entry | The secure side cannot discover the API through this Node |
| `wrap` | Replace the href with an HTTPS/WSS facade href | Opt-in. The facade terminates TLS, checks a bearer token, and forwards the request. It may rewrite `Location` and `Link` headers. Response bodies are forwarded unchanged |

`remove` is the default, so a secure Node does not advertise an insecure URL unless configuration says so. `expose` and `remove` are the policies operators are expected to use. `wrap` is basic transport security and a token check. It is not BCP-003-02 authorization for that type, and it does not interpret the body. A deployment that requires a fully secure boundary disallows `expose`.

## 6. Registration

Each secure Node is an ordinary nmos-cpp Node:

- `server_secure` and `server_authorization` on the APIs it serves
- `client_secure` and `client_authorization` when it registers
- one IS-10 client identity per Node, so BCP-003-02's client check on the secure Registry applies to that Node alone
- client-credentials grant, because the facade has no user interface
- DNS-SD advertisement of its secure APIs

Registration runs only when a secure Registry is discovered or configured. Absence of a Registry leaves the Node advertised and usable directly.

The island registration is untouched. Island Nodes keep registering with the island Registry over HTTP.

## 7. Program shape

A new long-running program next to `nmos-cpp-node` and `nmos-cpp-registry`, linked against nmos-cpp. One process holds:

- the Query subscription and the id-to-island-URL table
- one node model, listener set, and registration client per island Node
- an HTTP and WebSocket proxy used by §5

nmos-cpp already supplies the Node server, Query WebSocket client support, Registration behaviour, TLS, and IS-10. The new code is the subscription fan-out, URL rewrite, and proxy.

libnvnmos is the wrong base. Its model is built upwards from transport files and generated ids. This program ingests foreign IS-04 resources and keeps their ids.

## 8. Configuration

- Island Registry base URL (required), insecure. Query subscriptions are one WebSocket per resource type. An option selects the single empty-`resource_path` subscription when that Registry accepts it.
- Secure Registry base URL, or DNS-SD on the secure side. Optional.
- Facade hostname and addresses used in rewritten URLs.
- Port range: one HTTP(S) port per secure Node, allocated as Nodes appear and remembered for that Node id.
- TLS server certificate and CA, per Node or a default, as in nmos-cpp settings. Certificate provisioning (BCP-003-03) is the same follow-on as for any nmos-cpp Node; this program does not invent a second mechanism.
- Authorization Server, or DNS-SD. Client-credentials settings as in §6.
- Policy for other advertised services and controls (§5.2): `expose`, `remove`, or `wrap`, keyed by type URN with optional per-Node or per-Device overrides. Default `remove`. `wrap` is opt-in. A hardened deployment disallows `expose`.

## 9. Order of work

1. One island Node, IS-04 model from a Query subscription. URLs in the Node API resources and in the resources registered to the secure Registry are rewritten. Secure Node GETs are served from that model. Other `services` and `controls` entries are omitted until their facade exists.
2. IS-05, including an immediate activation, and `manifest_href`, with that API's authorization rules and with response-body and `Location` rewriting.
3. The remaining AMWA APIs nmos-cpp supports (IS-07, IS-08, IS-12, IS-13, IS-14), each with its own authorization rules and the same rewriting.
4. Many Nodes: appear, update, and disappear with the subscription, including Registry garbage-collection.
5. The three policies in §5.2 for other advertised services and controls. `wrap` is the opt-in basic proxy: TLS, a bearer-token check, and `Location` rewriting, with response bodies left unchanged.

## 10. Tests

- An insecure `nmos-cpp-node` registered only with an insecure `nmos-cpp-registry`.
- The facade and a secure Registry on the other side of the split.
- A secure client discovers the facade Node, reads the same Sender and Receiver ids, and sees group hints still grouped per Device.
- PATCH of Connection `/staged` reaches the island Node and the activation result returns to the client.
- Stopping the island Node's heartbeat removes the facade Node after Registry garbage-collection.
- The SDP fetched from the facade still names the island device's media addresses.
- A proxied response that carries a `Location` header, and a `controls` href or `manifest_href` in the Node API, both name the facade. The island host does not appear in those fields.

## 11. Open points

- Whether one listener port per Node is acceptable, or a single listener must demultiplex by host. One port per Node matches nmos-cpp and is the assumption in §8.
- Whether production configuration permits `expose` at all, or only a migration profile does.
- IS-05 activation deadline across the extra hop. The facade uses the island Node's response; it does not invent a shorter timeout.
- API versions. The facade advertises the versions the island Node registered, and refuses a secure call it cannot forward.
