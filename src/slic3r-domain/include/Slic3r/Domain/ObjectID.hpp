#pragma once

#include <cstddef>
#include <cstdint>


namespace Slic3r::Domain {
    class ObjectBase;
    class ObjectWithTimestamp;
}

namespace cereal{
template <class Archive> void serialize(Archive& ar, Slic3r::Domain::ObjectBase& base);
template <class Archive> void serialize(Archive& ar, Slic3r::Domain::ObjectWithTimestamp& object);
}

namespace Slic3r::Domain {

// Unique identifier of a mutable object accross the application.
// Used to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject)
// (for Model, ModelObject, ModelVolume or ModelInstance classes)
// and to serialize / deserialize an object onto the Undo / Redo stack.
// Valid IDs are strictly positive (non zero).
// It is declared as an object, as some compilers (notably msvcc) consider a typedef size_t equivalent to size_t
// for parameter overload.
class ObjectID
{
public:
    ObjectID(size_t id) : id(id) {}
    // Default constructor constructs an invalid ObjectID.
    ObjectID() : id(0) {}

    bool operator==(const ObjectID& rhs) const { return this->id == rhs.id; }
    bool operator!=(const ObjectID& rhs) const { return this->id != rhs.id; }
    bool operator< (const ObjectID& rhs) const { return this->id <  rhs.id; }
    bool operator> (const ObjectID& rhs) const { return this->id >  rhs.id; }
    bool operator<=(const ObjectID& rhs) const { return this->id <= rhs.id; }
    bool operator>=(const ObjectID& rhs) const { return this->id >= rhs.id; }

    bool valid() const { return id != 0; }
    bool invalid() const { return id == 0; }

    size_t  id;

private:
};

// Base for Model, ModelObject, ModelVolume or ModelInstance to provide a unique ID
// to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject).
// Also base for Print, PrintObject, SLAPrint, SLAPrintObject to provide a unique ID for matching Model / ModelObject
// with their corresponding Print / PrintObject objects by the notification center at the UI when processing back-end warnings.
// Achtung! The s_last_id counter is not thread safe, so it is expected, that the ObjectBase derived instances
// are only instantiated from the main thread.
class ObjectBase
{
public:
    using Timestamp = uint64_t;

    ObjectID            id() const { return m_id; }
    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual Timestamp   timestamp() const { return 0; }

protected:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    ObjectBase() : m_id(generate_new_id()) {}
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit ObjectBase(int) : m_id(ObjectID(0)) {}
    // The class tree will have virtual tables and type information.
    virtual ~ObjectBase() = default;

    // Use with caution!
    virtual void set_new_unique_id() { m_id = generate_new_id(); }
    // Use with caution!
    void         copy_id(const ObjectBase &rhs) { m_id = rhs.id(); }

    // Override this method if a ObjectBase derived class owns other ObjectBase derived instances.
    virtual void assign_new_unique_ids_recursive() { this->set_new_unique_id(); }

private:
    ObjectID                m_id;

    static inline ObjectID  generate_new_id() { return {++s_last_id}; }
    static size_t           s_last_id;

    template <class Archive>
    friend void cereal::serialize(Archive& ar, Slic3r::Domain::ObjectBase& base);
protected: // #vbCHECKME && #ysFIXME
    explicit ObjectBase(const ObjectID id) : m_id(id) {}
private:
};

class ObjectWithTimestamp : public ObjectBase
{
protected:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a new timestamp unique to this object's history.
    ObjectWithTimestamp() = default;
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit ObjectWithTimestamp(int) : ObjectBase(-1) {}
    // The class tree will have virtual tables and type information.
    ~ObjectWithTimestamp() override = default;

    // The timestamp uniquely identifies content of the derived class' data, therefore it makes sense to copy the timestamp if the content data was copied.
    void                copy_timestamp(const ObjectWithTimestamp& rhs) { m_timestamp = rhs.m_timestamp; }

public:
    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    Timestamp        timestamp() const noexcept override { return m_timestamp; }
    bool             timestamp_matches(const ObjectWithTimestamp &rhs) const noexcept { return m_timestamp == rhs.m_timestamp; }
    bool             object_id_and_timestamp_match(const ObjectWithTimestamp &rhs) const noexcept { return this->id() == rhs.id() && m_timestamp == rhs.m_timestamp; }
    void             touch() { m_timestamp = ++ s_last_timestamp; }

private:
    // The first timestamp is non-zero, as zero timestamp means the timestamp is not reliable.
    Timestamp        m_timestamp { 1 };
    static Timestamp s_last_timestamp;

    template <class Archive>
    friend void cereal::serialize(Archive& ar, Slic3r::Domain::ObjectWithTimestamp& object);
};

// Unique object / instance ID for the wipe tower.
ObjectID wipe_tower_instance_id(size_t bed_idx);

} // namespace Slic3r::Domain
