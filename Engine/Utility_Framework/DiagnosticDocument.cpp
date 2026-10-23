#include "DiagnosticDocument.h"
#include "AuthoringRymlErrorPolicy.h"
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/yml/emit.hpp>
#include <c4/format.hpp>
#include <stdexcept>
namespace Diagnostics
{
    struct DocumentState { ryml::Tree tree; };
    namespace
    {
        ryml::NodeRef Ref(DocumentState* state, std::size_t id)
        {
            if (!state || id >= state->tree.size()) throw std::logic_error("Invalid diagnostic document node");
            return state->tree.ref(id);
        }
    }
    Document::Document() : state_(std::make_unique<DocumentState>()) {Authoring::EnsureRymlErrorPolicy();}
    Document::~Document()=default;
    Document::Document(Document&&) noexcept=default;
    Document& Document::operator=(Document&&) noexcept=default;
    Node Document::Root()
    {
        if (!state_) throw std::logic_error("Moved diagnostic document");
        return Node(state_.get(),state_->tree.rootref().id());
    }
    std::string Document::ToJson() const
    {
        if (!state_) throw std::logic_error("Moved diagnostic document");
        return ryml::emitrs_json<std::string>(state_->tree);
    }
    Node Node::operator[](std::string_view key) const
    {
        auto parent=Ref(state_,id_);
        if (!parent.is_map()) parent |= ryml::MAP;
        const ryml::csubstr text(key.data(),key.size());
        if (parent.has_child(text)) return Node(state_,parent[text].id());
        auto child=parent.append_child();child.set_key(state_->tree.copy_to_arena(text));
        return Node(state_,child.id());
    }
    Node Node::operator[](std::size_t index) const
    {
        auto parent=Ref(state_,id_);
        if (!parent.is_seq() || index>=parent.num_children()) throw std::out_of_range("Diagnostic sequence index");
        return Node(state_,parent[index].id());
    }
    Node Node::Append() const
    {
        auto parent=Ref(state_,id_);
        if (!parent.is_seq()) parent |= ryml::SEQ;
        return Node(state_,parent.append_child().id());
    }
    bool Node::Has(std::string_view key) const
    {
        const auto parent=Ref(state_,id_);
        return parent.is_map() && parent.has_child(ryml::csubstr(key.data(),key.size()));
    }
    void Node::SetMap() const {Ref(state_,id_) |= ryml::MAP;}
    void Node::SetSequence() const {Ref(state_,id_) |= ryml::SEQ;}
    void Node::operator<<(std::string_view value) const
    {
        Ref(state_,id_).set_val(state_->tree.copy_to_arena(ryml::csubstr(value.data(),value.size())),ryml::NodeType{ryml::VAL_DQUO});
    }
    void Node::operator<<(bool value) const {Ref(state_,id_) << c4::fmt::boolalpha(value);}
    void Node::SetSigned(std::int64_t value) const {Ref(state_,id_) << value;}
    void Node::SetUnsigned(std::uint64_t value) const {Ref(state_,id_) << value;}
    void Node::SetFloat(double value) const {Ref(state_,id_) << value;}
}
