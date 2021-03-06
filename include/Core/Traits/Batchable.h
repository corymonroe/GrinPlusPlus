#pragma once

#include <memory>

namespace Traits
{
	class IBatchable
	{
	public:
		IBatchable()
			: m_dirty(false)
		{

		}

		virtual ~IBatchable() = default;

		virtual void Commit() = 0;
		virtual void Rollback() = 0;

		// This can be overridden
		virtual void OnInitWrite() { }

		// This can be overridden
		virtual void OnEndWrite() { }

		bool IsDirty() const { return m_dirty; }
		void SetDirty(const bool dirty) { m_dirty = dirty; }

	private:
		bool m_dirty;
	};
}