#pragma once

#include <shared_mutex>

#include <Client.h>
#include <ComponentHolder.h>

#ifndef FOLLY_NO_CONFIG
#define FOLLY_NO_CONFIG
#endif

#if defined(ssize_t)
#undef ssize_t
#endif
#include <folly/SharedMutex.h>

#include <tbb/concurrent_unordered_map.h>

namespace tbb
{
	namespace interface5
	{
		template<>
		inline size_t tbb_hasher(const net::PeerAddress& addr)
		{
			return std::hash<std::string_view>()(std::string_view{ (const char*)addr.GetSocketAddress(), (size_t)addr.GetSocketAddressLength() });
		}
	}
}

namespace fx
{
	class ServerInstanceBase;

	class ClientRegistry : public fwRefCountable, public IAttached<ServerInstanceBase>
	{
	public:
		ClientRegistry();

		// invoked upon receiving the `connect` ENet packet
		void HandleConnectingClient(const fx::ClientSharedPtr& client);

		// invoked upon receiving the `connect` ENet packet, after sending `connectOK`
		void HandleConnectedClient(const fx::ClientSharedPtr& client, uint32_t oldNetID);

		fx::ClientSharedPtr MakeClient(const std::string& guid);

		inline void RemoveClient(const fx::ClientSharedPtr& client)
		{
			m_clientsByPeer[client->GetPeer()].reset();
			m_clientsByNetId[client->GetNetId()].reset();
			m_clientsByConnectionToken[client->GetConnectionToken()].reset();

			if (client->GetSlotId() >= 0 && client->GetSlotId() < m_clientsBySlotId.size())
			{
				std::lock_guard clientGuard(m_clientSlotMutex);
				m_clientsBySlotId[client->GetSlotId()].reset();
			}

			{
				folly::SharedMutex::WriteHolder writeHolder(m_clientMutex);
				m_clients.erase(client->GetGuid());
			}

			// unassign slot ID
			client->SetSlotId(-1);
		}

		inline fx::ClientSharedPtr GetClientByGuid(const std::string& guid)
		{
			auto ptr = fx::ClientSharedPtr();

			{
				folly::SharedMutex::ReadHolder readHolder(m_clientMutex);

				auto it = m_clients.find(guid);
				if (it != m_clients.end())
				{
					ptr = it->second;
				}
			}

			return ptr;
		}

		inline fx::ClientSharedPtr GetClientByPeer(int peer)
		{
			auto ptr = fx::ClientSharedPtr();
			auto it = m_clientsByPeer.find(peer);

			if (it != m_clientsByPeer.end())
			{
				ptr = it->second.lock();
			}

			return ptr;
		}

		inline fx::ClientSharedPtr GetClientByEndPoint(const net::PeerAddress& address)
		{
			auto ptr = fx::ClientSharedPtr();
			auto it = m_clientsByEndPoint.find(address);

			if (it != m_clientsByEndPoint.end())
			{
				ptr = it->second.lock();
			}

			return ptr;
		}

		inline fx::ClientSharedPtr GetClientByTcpEndPoint(const std::string& address)
		{
			auto ptr = fx::ClientSharedPtr();
			auto it = m_clientsByTcpEndPoint.find(address);

			if (it != m_clientsByTcpEndPoint.end())
			{
				ptr = it->second.lock();
			}

			return ptr;
		}

		inline fx::ClientSharedPtr GetClientByNetID(uint32_t netId)
		{
			auto ptr = fx::ClientSharedPtr();
			auto it = m_clientsByNetId.find(netId);

			if (it != m_clientsByNetId.end())
			{
				ptr = it->second.lock();
			}

			return ptr;
		}

		inline fx::ClientSharedPtr GetClientBySlotID(uint32_t slotId)
		{
			assert(slotId < m_clientsBySlotId.size());

			std::lock_guard clientGuard(m_clientSlotMutex);
			return m_clientsBySlotId[slotId].lock();
		}

		inline fx::ClientSharedPtr GetClientByConnectionToken(const std::string& token)
		{
			auto ptr = fx::ClientSharedPtr();
			auto it = m_clientsByConnectionToken.find(token);

			if (it != m_clientsByConnectionToken.end())
			{
				ptr = it->second.lock();
			}

			return ptr;
		}

		template<typename TFn>
		inline void ForAllClientsLocked(TFn&& cb)
		{
			folly::SharedMutex::ReadHolder readHolder(m_clientMutex);
			for (const auto& [guid, client] : m_clients)
			{
				if (client->IsDropping())
				{
					continue;
				}

				cb(client);
			}
		}

		template<typename TFn>
		inline void ForAllClients(TFn&& cb)
		{
			std::vector<fx::ClientSharedPtr> allClients{};

			{
				folly::SharedMutex::ReadHolder readHolder(m_clientMutex);
				allClients.reserve(m_clients.size());

				for (const auto& [guid, client] : m_clients)
				{
					if (client->IsDropping())
					{
						continue;
					}

					allClients.push_back(client);
				}
			}

			for (const auto& client : allClients)
			{
				cb(client);
			}
		}

		fx::ClientSharedPtr GetHost();

		void SetHost(const fx::ClientSharedPtr& client);

		virtual void AttachToObject(ServerInstanceBase* instance) override;

		fwEvent<const fx::ClientSharedPtr&> OnClientCreated;

		fwEvent<Client*> OnConnectedClient;

	private:
		uint16_t m_hostNetId;

		folly::SharedMutex m_clientMutex;
		std::unordered_map<std::string, fx::ClientSharedPtr> m_clients;

		// aliases for fast lookup
		tbb::concurrent_unordered_map<uint32_t, fx::ClientWeakPtr> m_clientsByNetId;
		tbb::concurrent_unordered_map<net::PeerAddress, fx::ClientWeakPtr> m_clientsByEndPoint;
		tbb::concurrent_unordered_map<std::string, fx::ClientWeakPtr> m_clientsByTcpEndPoint;
		tbb::concurrent_unordered_map<std::string, fx::ClientWeakPtr> m_clientsByConnectionToken;
		tbb::concurrent_unordered_map<int, fx::ClientWeakPtr> m_clientsByPeer;

		std::mutex m_clientSlotMutex;
		std::vector<fx::ClientWeakPtr> m_clientsBySlotId;

		std::mutex m_curNetIdMutex;
		uint16_t m_curNetId;

		ServerInstanceBase* m_instance;
	};
}

DECLARE_INSTANCE_TYPE(fx::ClientRegistry);
