<!--
SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# nmos-cpp Registry Write Scaling

Status: draft for discussion.

This document covers the write side of the nmos-cpp registry: heartbeats,
registrations, and recovery after a network outage. It describes how the
registry behaves today, proposes improvements in priority order, and compares
options for running more than one registry instance. The test harness that
will produce the measurements is described in the
[Registry Load Test Harness](https://github.com/garethsb/nmos-cpp-examples/blob/registry-scaling/registry-scaling/harness.md)
document.

Each section is labelled as current behaviour, proposal, or open question.
Current behaviour refers to [sony/nmos-cpp](https://github.com/sony/nmos-cpp)
at commit `5bcfb2c8`.

## Goals

- A registry with N Nodes keeps every heartbeat well inside the Node's
  heartbeat timeout, so no healthy Node is expired.
- After a facility-wide network outage, the registry returns to the state
  before the outage without expiring Nodes that are still present.
- After a partial outage, or after an outage long enough that expiry is
  correct, the registry absorbs the re-registrations in bounded time.
- Improvements help every IS-04 Node implementation, not only nmos-cpp Nodes.

Every proposal below is judged by harness results. A proposal that does not
move a measured metric is dropped.

## Current Behaviour

### Locking

The registry model has one `nmos::mutex`, a `bst::shared_mutex` (either
`std::shared_mutex` or `boost::shared_mutex`, depending on the build). Every
handler that reads or changes `registry_resources` holds it:

| Handler | Lock |
|---|---|
| Registration API heartbeat (`POST /health/nodes/{id}`) | Read |
| Registration API `GET /resource/...` | Read |
| Registration API `POST /resource` | Write |
| Registration API `DELETE /resource/...` | Write |
| Query API `GET` | Read |
| Query API subscription `POST` and `DELETE` | Write |
| Query WebSocket validate, open and close | Write |
| `send_query_ws_events_thread` | Write, released while sending |
| `erase_expired_resources_thread` | Read, then released and re-acquired as write to delete |

The handlers run on cpprestsdk's shared thread pool, which has 40 threads by
default (`crossplat::threadpool::shared_instance`). A handler that is waiting
for the model mutex keeps a pool thread busy while it waits.

Open question: whether readers wait behind a queued writer depends on the
`shared_mutex` implementation and platform. The harness should establish this
for the builds that matter, because it decides whether heartbeats queue behind
registrations.

### Heartbeats

`resource::health` is a `mutable` atomic, so the heartbeat handler needs only a
read lock. It finds the Node and calls `set_resource_health`, which sets the
health of the Node and, recursively, all its sub-resources.

The Node's heartbeat timeout is `registration_heartbeat_max`, 5 seconds by
default. The heartbeat interval is `registration_heartbeat_interval`, 5 seconds
by default.

### Registration

`POST /resource` does the following:

1. Reads the request body. No lock is held.
2. Takes the write lock.
3. Validates the body against the IS-04 schema. The validator serializes the
   cpprest JSON value to a string, parses it with nlohmann JSON, and runs the
   pboettch json-schema-validator.
4. Checks the request against the model: type, API version, super-resource,
   version timestamp, and the ordering of requests.
5. Logs the registration at `info` level. In `nmos-cpp-registry`, the logging
   gateway formats the message on the calling thread and posts it to a queue
   without blocking. A worker thread writes the logs, so no log I/O happens
   while the write lock is held.
6. Inserts or modifies the resource, which also generates Query API events
   (see below).
7. Notifies the model's condition variable.

The Node's timeout for `/resource` requests is `registration_request_max`, 30
seconds by default.

### Query API Events

`insert_resource`, `modify_resource` and `erase_resource` call
`insert_resource_events`. For every subscription in the model, it constructs a
`resource_query` from the subscription's `resource_path` and parameters,
evaluates the query against the resource before and after the change, and
appends an event to the grain of each WebSocket connection on that
subscription. The work for each change is proportional to the number of
subscriptions, and it is done while the write lock is held.

`send_query_ws_events_thread` builds and serializes each outgoing message while
holding the write lock, then releases it to send.

### Expiry

`erase_expired_resources_thread` wakes when the least healthy resource could
have expired. A Node expires when it has sent no heartbeat for
`registration_expiry_interval`, 12 seconds by default. The thread erases every
resource whose own health has expired, sub-resources before their parents.
Because each heartbeat sets the same health on the Node's sub-resources, a
Node and its sub-resources expire together. Each erasure generates a Query API
event. Erased resources are kept as tombstones and forgotten after a further
expiry interval.

By contrast, an explicit `DELETE` through the Registration API calls
`erase_resource`, which erases the resource's sub-resources recursively.

### nmos-cpp Node Behaviour

Other Node implementations differ in detail. IS-04 specifies the main
behaviour, and nmos-cpp follows it:

- A 404 response to a heartbeat means the registry does not know the Node. The
  Node re-registers all its resources, in order, one request at a time.
- A 5xx response, a failure to connect, or a timeout means a server-side or
  connectivity problem. The Node moves to the next Registration API, and its
  first request there is a heartbeat. It re-registers only if that heartbeat
  returns 404.
- When every known Registration API has failed, the Node repeats discovery with
  a randomized exponential backoff between `discovery_backoff_min` and
  `discovery_backoff_max` (1 and 30 seconds by default, factor 1.5).

### How a Re-registration Storm Develops

This is the expected sequence for a facility-wide outage longer than the expiry
interval. Parts of it are hypotheses for the harness to confirm.

1. No heartbeats arrive. The Nodes' last heartbeats fall within one heartbeat
   interval before the outage, so all Nodes become due for expiry within that
   same interval.
2. The expiry thread erases every Node and its sub-resources. Each erasure
   generates an event for each subscription.
3. The network returns. Every Node's next heartbeat returns 404, and every Node
   starts re-registering, so the registry receives about one concurrent
   request per Node.
4. Each registration holds the write lock for the whole of validation, the
   model checks, and event generation.
5. Hypothesis: heartbeats from Nodes that have already re-registered wait for
   the read lock, or for a free pool thread, behind the queued registrations.
6. Hypothesis: if a heartbeat waits longer than the 5-second heartbeat timeout,
   the Node treats it as a server-side failure, moves on or rediscovers, and
   may be expired again. The storm then sustains itself.

An outage shorter than the expiry interval causes no storm: heartbeats resume
and return 200.

## Proposed Improvements, in Priority Order

The order reflects expected benefit against effort. The harness baseline may
change it.

### 1. Guard Against Mass Expiry

Proposal. Detect a suspected network fault and hold off expiry, so that a
facility-wide outage causes no storm.

- Trigger: more than a configured fraction of Nodes, and more than a minimum
  number of Nodes, have sent no heartbeat for longer than one heartbeat
  interval. The registry does not know each Node's heartbeat interval, so this
  needs a registry setting, defaulting to the 5 seconds that IS-04 recommends.
- While the hold is in force, the expiry thread erases nothing and heartbeats
  are accepted normally. If the registry's own network link fails, it sees
  every Node go silent, so that case is covered too.
- When heartbeats resume and the silent fraction falls below the threshold, the
  hold ends. Each Node that is still silent then gets a fresh grace period of
  one expiry interval before it is erased.

Cost: small to medium, all in the expiry thread and settings.

Risk: during the hold, the Query API shows resources that may have gone. This
deliberately departs from the IS-04 recommendation to garbage-collect after 12
seconds, so the guard is configurable.

Measured by: the long facility-wide outage scenario. Expect no false expiries,
no 404 heartbeats, and recovery within a few heartbeat intervals.

Open questions: the default threshold and minimum count; whether the guard is
enabled by default; whether a partial outage near the threshold behaves
acceptably.

### 2. Make Heartbeats Independent of the Model Lock

Proposal. Health is already atomic. Only the lookup from Node ID to resource
needs the model lock. A separate index from Node ID to a shared health value,
with its own lock held only briefly, would let a heartbeat complete without
waiting for the model mutex. The expiry thread would read health through the
same shared values.

Cost: medium. The index must be kept in step with insertion and erasure of
Nodes, and the health of sub-resources must still be updated or derived from
the Node.

Risk: subtle lifetime and consistency issues between the index and the model.

Measured by: heartbeat latency (p99 and maximum) during cold start and storm
scenarios. This proposal targets the self-sustaining step of the storm, and
helps under any heavy write load, not only after outages.

### 3. Validate Before Taking the Write Lock

Proposal. Schema validation uses only the request body. The validator loads all
schemas when it is constructed, and `validate` is `const`. Validate first with
no lock held, then take the write lock as now. Read `allow_invalid_resources`
after taking the lock and apply it to the stored validation result. No read
lock and no lock upgrade are involved.

The benefit applies to valid registrations, because the point is to move the
serialize, parse and validate work out of the locked section, not to reject
invalid requests early.

Cost: small.

Risk: concurrent `const` validation must be safe in the pboettch library. To
confirm.

Measured by: write lock hold time per registration, and registration
throughput in the cold start scenario.

### 4. Reduce Event Generation Work Under the Lock

Proposal, in two steps.

Step 1: in `insert_resource_events`, compare the subscription's `resource_path`
with the resource type before constructing the `resource_query`. A Sender
change then skips every subscription on another resource type for the cost of
a string comparison. Cost: small.

Step 2, only if profiling still shows query construction: cache the parsed
query for each subscription. Subscriptions cannot be modified after creation,
so a cache keyed by subscription ID never goes stale; it only needs pruning.
`insert_resource_events` receives only `nmos::resources&` and is also used for
Node models, so the cache location is the main design choice:

| Location | Trade-off |
|---|---|
| A member of `nmos::resource` | Touches a core type used everywhere |
| Owned by the model and passed in | Changes the signatures of `insert_resource` and related functions |
| A process-wide map | Simple, but global state shared by every model in the process |

Measured by: write lock hold time per registration, and storm recovery time,
each with no subscriptions and with many.

### 5. Shed Registration Load Deliberately

Proposal. When the write lock wait exceeds a threshold, respond to
`POST /resource` with 503 instead of letting requests queue. IS-04 recommends
exponential backoff on 5xx, so this turns a storm into a gradual ramp for every
Node that follows the specification. Heartbeats are never shed: a 5xx on a
heartbeat makes a Node change registry.

Cost: medium.

Risk: an nmos-cpp Node that receives a 5xx during initial registration moves to
the next registry. With a single registry, it rediscovers and backs off. The
harness should confirm that this ramps rather than oscillates.

Supporting `Retry-After` in nmos-cpp Nodes would be a separate and optional
addition.

### 6. Investigate Using Harness Profiles

Hypotheses to confirm or rule out before proposing changes:

- Logging while holding the write lock. Log I/O is already on a worker
  thread, so what remains is formatting each message and posting it to the
  logging queue, which locks the queue's mutex briefly. Expected to be small;
  worth confirming at the logging levels used in production.
- Exhausting the 40-thread pool with handlers that are waiting for the mutex.
- Serializing WebSocket messages while holding the write lock.

## Options for More Than One Instance

These options matter for high availability and for partial outages. For write
scaling alone, the single-process improvements above come first, because every
instance of a replicated registry still applies every change to its own copy of
the model.

### Option Summary

| Option | Write scaling | Outage affecting one instance | nmos-cpp changes | Operating cost |
|---|---|---|---|---|
| One instance, hot restart, local persistence | None | Affected Nodes re-register | Persistence of `registry_resources` | Low |
| Active-passive, shared store | None | Failover without re-registration | Store, replication, leader election | etcd cluster |
| One writer, many readers, shared store | None; reads scale | Failover without re-registration | As above, plus forwarding writes to the leader | etcd cluster |
| Active-active, shared store | HTTP, TLS and validation spread across instances; each instance still applies every change | Failover without re-registration | As above, plus distributed heartbeats and paging timestamps | etcd cluster |

"Failover without re-registration" relies on the IS-04 rule that a Node's first
request to a new registry is a heartbeat. With a shared store, the new instance
already knows the Node, the heartbeat succeeds, and the Node carries on.
Independent registries cannot do this.

No option avoids the storm after a facility-wide outage longer than the expiry
interval. Every option needs the mass-expiry guard for that case.

### Options in Detail

Proposals. Each option describes how it handles heartbeats and paging
timestamps, which are the two problems that make a shared registry hard.

#### One Instance with Hot Restart

The registry persists registered resources locally, for example to a file or
an embedded database, and reloads them on restart. etcd is not needed.

- Without persistence, a restart already recovers under IS-04: heartbeats
  return 404 and Nodes re-register within about one heartbeat interval.
  Persistence avoids that re-registration and the matching churn of Query API
  events. Query WebSocket connections are still lost.
- On reload, every Node gets a fresh grace period by setting its health to the
  reload time. Restoring the saved health values would expire every Node after
  a restart longer than the expiry interval.
- Paging timestamps: one writer, so `strictly_increasing_update` continues from
  the highest reloaded value.
- The lease-based heartbeat design below does not suit this option, because
  leases expire while no registry is running.

#### Active-Passive

One active instance serves every API. A standby watches the shared store, so
its copy of the model is current when it takes over. An etcd lease decides
which instance is active.

- Heartbeats stay in the active instance's memory, as now, so heartbeats cause
  no etcd writes. On takeover, the new active instance gives every Node a fresh
  grace period. Expiry may be up to one expiry interval late after a failover,
  but never early.
- Paging timestamps: one writer records `created` and `updated` in the store,
  and a new active instance continues from the highest stored value.
- With DNS-SD, both instances can stay advertised. The standby returns 503, so
  Nodes skip it and move to the next registry. This avoids changing DNS-SD
  advertisements on failover, which with mDNS caching or unicast DNS TTLs can
  be slow.
- Behind a load balancer, the load balancer's health check selects the active
  instance.

#### One Writer, Many Readers

As active-passive, except that the followers also serve the Query API and
Query WebSockets from their own copy of the model, and forward Registration
API requests, including heartbeats, to the leader.

- Heartbeats and paging timestamps are as simple as in active-passive.
- Followers use the leader's stored timestamps, and cap paging at the
  timestamp of the last change they have applied, so a page never ends after a
  change the follower has not yet seen.
- Query load and WebSocket connections are spread across instances.
- With DNS-SD, followers can advertise their Registration API too, because
  they forward rather than refuse.

#### Active-Active

Every instance accepts writes, using the shared store design below in full:
lease-based heartbeats with guarded deletes, and either instance-local paging
timestamps with affinity or a shared clock.

### Discovery

Open question: whether instances are discovered individually through DNS-SD or
placed behind a load balancer with one address.

- With DNS-SD, each client stays with one instance until it fails. That gives
  affinity without extra machinery, so instance-local paging cursors and
  subscriptions work.
- Behind a load balancer, the subscription's `ws_href` is built from the
  request's `Host` header, so the WebSocket connection returns through the load
  balancer and may reach another instance. That needs load balancer affinity or
  shared subscriptions. Paging `Link` headers are also built from `Host`, so
  later pages may reach another instance, which needs affinity or shared paging
  timestamps.

A Kubernetes deployment usually expects a single Service address, which
behaves like the load balancer case unless each instance is also exposed and
advertised individually.

### Choice of Store

Proposal: etcd.

| Need | etcd | Redis |
|---|---|---|
| Acknowledged writes survive failover | Yes; writes are committed through Raft consensus | Not by default; replication is asynchronous, and failover can lose acknowledged writes |
| Ordered change history | Cluster-wide revision on every change; a watch can resume from a past revision until it is compacted | Streams give an ordered log with consumer offsets; keyspace notifications are fire-and-forget and can be missed |
| Atomic conditional updates | Transactions that compare key values, versions or revisions | `MULTI`/`EXEC` with `WATCH`, or Lua scripts and functions |
| Expiry | Leases, which many keys can share; a keep-alive renews them all | A TTL on each key |

Registry data is small: thousands of resources of a few kilobytes each. The
deciding needs are acknowledged writes that survive failover and an ordered
change history that a restarting instance can resume from, and etcd provides
both directly. Redis would need Streams for the change history and careful
handling of failover. It would be worth reconsidering only where Redis is
already operated and etcd is not.

The registry's etcd cluster should be separate from any Kubernetes control
plane etcd.

### Design Notes for a Shared Store

Proposal, for the active-active option. The simpler options need only parts of
it.

- Replicate only the registered resource types: Nodes, Devices, Sources, Flows,
  Senders and Receivers. Subscriptions, grains and the registry's own
  resources share `registry_resources` but stay local to each instance.
  Whether subscriptions are shared is an open question tied to discovery.
- Commit each change to etcd first. Each instance's watcher applies changes
  through `insert_resource`, `modify_resource` and `erase_resource`, so
  `insert_resource_events` generates WebSocket events with no change to the
  Query WebSocket code.
- Heartbeats: each Node has an etcd lease attached only to a health key, not to
  its resource keys. A heartbeat to any instance is a lease keep-alive, which
  is not a Raft write. When a lease expires, etcd deletes only the health key.
  Each instance then attempts an ordinary delete transaction for the Node and
  its sub-resources, conditional on the Node existing and the health key being
  absent. Exactly one attempt succeeds, so no leader election is needed. The
  mass-expiry guard fits this design: lease expiry marks a Node as a candidate,
  and a heartbeat for a Node whose record still exists grants a new lease.
- Paging timestamps: `created` and `updated` are assigned by
  `strictly_increasing_update` on each instance, so they differ between
  instances. Either keep them local and rely on affinity, or assign them at
  commit using a clock key in etcd, so that they increase in revision order.
  The second approach also needs a non-overwriting insert path and a paging
  upper bound equal to the timestamp of the last applied change, rather than
  `most_recent_update` over all resources.
- When an etcd leader changes, leases are renewed to their full TTL, so expiry
  can be up to one TTL late but never early.
- Read-your-writes: the instance that accepts a registration responds only
  after etcd has committed it and the instance's own watcher has applied it.
  It does not wait for other instances. A Node that registers a Device
  immediately after its Node therefore finds the Node in the same instance's
  model.
- Startup and recovery: an instance reads the whole resource prefix at
  revision R, then watches from revision R + 1. If etcd has compacted past the
  watch position, the instance reads a new snapshot and rebuilds. It never
  skips changes. A rebuild must end every open Query WebSocket connection, or
  send each one a full set of events, because the connections cannot be told
  which changes were missed.
- Registration checks: today the checks for an existing super-resource and an
  unchanged type run against the local model under the write lock. With more
  than one writer, those checks race between instances. The commit
  transaction must compare the keys it depends on, for example the parent's
  key and the resource's own key, and the registration fails or retries if
  they changed.
- Deletes that cascade: `erase_resource` erases a resource's sub-resources
  locally and generates a Query API event for each. The store must record
  either every child delete or only the parent delete, with each instance's
  watcher cascading locally. Doing both applies each child delete twice. The
  guarded expiry transaction above has the same choice to make.
- Tombstones: an instance that starts from a snapshot has no record of
  resources erased before the snapshot, so for those resources it returns a
  plain 404 rather than the more specific "erased" response. This is a minor
  difference and probably acceptable.

### Code Paths That Write `registry_resources`

Current behaviour. Each path needs a decision: commit through the store, or
stay local.

| Code path | Resources written | With a shared store |
|---|---|---|
| Registration API `POST /resource` and `DELETE /resource/...` | Registered resources | Commit through the store |
| Registration API heartbeat | Health of a Node and its sub-resources | Depends on the option; see above |
| `erase_expired_resources_thread` | Registered resources and subscriptions | Registered resources through the store; subscriptions local |
| Query API subscription `POST` and `DELETE` | Subscriptions | Local, unless subscriptions are shared |
| Query WebSocket open and close | Grains, and the subscription's sub-resources and health | Local |
| `send_query_ws_events_thread` | Grains | Local |
| `insert_resource_events` | Grains, as a side effect of every change | Local, run by the watcher |
| Registry startup (`insert_registry_resources`) | The registry's own resources | Local |

Applications built on nmos-cpp may also write to `registry_resources`
directly. Those writes would bypass the store.

### Cost and Benefit

Proposal. Effort is relative, and assumes the single-process improvements are
already in place.

| Option | Effort | Main benefit | Main cost |
|---|---|---|---|
| Hot restart | Small: persistence and reload of registered resources | No re-registration after a restart | Persistence on every write |
| Active-passive | Large: store, watcher, leader election, takeover | Fast failover; no re-registration after host failure or an outage affecting one instance | etcd cluster; a standby that serves nothing |
| One writer, many readers | Large, plus forwarding | As active-passive, and Query load spread across instances | As active-passive, plus a forwarding hop for writes |
| Active-active | Largest: distributed heartbeats, registration checks in transactions, paging timestamps | Writes accepted by every instance | Every instance applies every change; more failure modes to test |

The write scaling that active-active adds is limited. After improvement 3, a
single process already validates registrations in parallel across all cores.
What remains serialized is applying each change to the model and generating
its events, and active-active does not parallelize that: every instance applies
every change. Active-active helps writes when one host's cores, network or
connection limits are not enough, or when generating events dominates,
because each instance generates events only for its own subscriptions.

Recommendation, to be confirmed by harness results:

1. Make the single-process improvements, starting with the mass-expiry guard
   and heartbeats independent of the model lock.
2. If the harness shows that host failure or outages affecting one instance
   matter, build active-passive, with a store design that one writer with many
   readers, and later active-active, can extend.
3. Move to active-active only if the harness shows a single writer saturating
   after the single-process improvements.

Evidence that would justify each option:

| Option | Justified when the harness shows |
|---|---|
| Hot restart | Recovery after a registry restart is too slow, or the churn of Query API events is a problem |
| Active-passive | Recovery after host failure or after an outage affecting one instance is too slow |
| One writer, many readers | Query load or WebSocket connections saturate one instance while writes do not |
| Active-active | A single writer saturates on registrations or heartbeats after the single-process improvements |

## Open Questions

- Which failure matters most in practice: facility-wide outages, partial
  outages, or registry host failure? The answer decides whether a shared store
  is needed at all.
- What N, resources per Node and subscription counts should the targets use?
- Should the mass-expiry guard be enabled by default?
- Should these changes be proposed upstream to sony/nmos-cpp individually, in
  the order above?
- Will deployments be discovered through DNS-SD, placed behind a load
  balancer, or exposed as a single Kubernetes Service address?
- Must persistent Query API subscriptions survive a failover to another
  instance?
- What consistency should Query clients be promised when a follower's copy of
  the model is slightly behind the leader's?
