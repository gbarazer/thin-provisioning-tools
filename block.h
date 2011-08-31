#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <map>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include <string.h>

//----------------------------------------------------------------

namespace persistent_data {

	uint32_t const MD_BLOCK_SIZE = 4096;

	class count_adjuster {
	public:
		count_adjuster(unsigned &c)
			: c_(c) {
			c_++;
		}

		~count_adjuster() {
			c_--;
		}

	private:
		unsigned &c_;
	};

	typedef uint64_t block_address;

	template <uint32_t BlockSize = MD_BLOCK_SIZE>
	class block_manager : private boost::noncopyable {
	public:
		typedef boost::shared_ptr<block_manager> ptr;

		block_manager(std::string const &path, block_address nr_blocks);
		~block_manager();

		typedef unsigned char buffer[BlockSize];
		typedef unsigned char const const_buffer[BlockSize];

		class block;

		class validator {
		public:
			typedef boost::shared_ptr<validator> ptr;

			virtual ~validator() {}

			virtual void check(block const &b) const = 0;
			virtual void prepare(block &b) const = 0;
		};

		class noop_validator : public validator {
		public:
			void check(block const &b) const {}
			void prepare(block &b) const {}
		};

		enum block_type {
			BT_SUPERBLOCK,
			BT_NORMAL
		};

		struct block {
			typedef boost::shared_ptr<block> ptr;

			block(block_address location,
			      const_buffer &data,
			      unsigned &count,
			      unsigned &type_count,
			      block_type bt,
			      typename validator::ptr v)
				: location_(location),
				  adjuster_(count),
				  type_adjuster_(type_count),
				  validator_(v),
				  bt_(bt) {
				::memcpy(data_, data, sizeof(data));
			}

			block_address location_;
			count_adjuster adjuster_;
			count_adjuster type_adjuster_;
			buffer data_;
			typename validator::ptr validator_;
			block_type bt_;
		};

		class read_ref {
		public:
			read_ref(typename block::ptr b);
			virtual ~read_ref() {}

			block_address get_location() const;
			const_buffer &data() const;

		protected:
			friend class block_manager;
			typename block::ptr block_;
		};

		// Inherited from read_ref, since you can read a block that's write
		// locked.
		class write_ref : public read_ref {
		public:
			write_ref(typename block::ptr b);

			using read_ref::data;
			buffer &data();
		};

		// Locking methods
		read_ref
		read_lock(block_address location,
			  typename validator::ptr v =
			  typename validator::ptr(new noop_validator())) const;

		boost::optional<read_ref>
		read_try_lock(block_address location,
			      typename validator::ptr v =
			      typename validator::ptr(new noop_validator())) const;

		write_ref
		write_lock(block_address location,
			   typename validator::ptr v =
			   typename validator::ptr(new noop_validator()));

		write_ref
		write_lock_zero(block_address location,
				typename validator::ptr v =
				typename validator::ptr(new noop_validator()));

		// The super block is the one that should be written last.
		// Unlocking this block triggers the following events:
		//
		// i) synchronous write of all dirty blocks _except_ the
		// superblock.
		//
		// ii) synchronous write of superblock
		//
		// If any locks are held at the time of the superblock
		// being unlocked then an exception will be thrown.
		write_ref superblock(block_address b,
				     typename validator::ptr v =
				     typename validator::ptr(new noop_validator()));
		write_ref superblock_zero(block_address b,
					  typename validator::ptr v =
					  typename validator::ptr(new noop_validator()));

		block_address get_nr_blocks() const;

	private:
		void check(block_address b) const;

		void read_buffer(block_address location, buffer &buf) const;
		void write_buffer(block_address location, const_buffer &buf);
		void zero_buffer(buffer &buf) const;
		void read_release(block *b) const;
		void write_release(block *b);

		enum lock_type {
			READ_LOCK,
			WRITE_LOCK
		};

		void register_lock(block_address b, lock_type t) const;
		void unregister_lock(block_address b, lock_type t) const;

		int fd_;
		block_address nr_blocks_;
		mutable unsigned lock_count_;
		mutable unsigned superblock_count_;
		mutable unsigned ordinary_count_;

		typedef std::map<block_address, std::pair<lock_type, unsigned> > held_map;
		mutable held_map held_locks_;
	};
}

#include "block.tcc"

//----------------------------------------------------------------

#endif
