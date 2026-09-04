#include "Slic3r/Biz/Expr/Simplify.hpp"

#include <vector>
#include <algorithm>
#include <map>
#include <optional>
#include <cmath>
#include <ranges>
#include <boost/variant/get.hpp>

namespace Slic3r::Biz::Expr {

namespace {

using namespace Slic3r::Domain::Expr;

// Helper to represent a normalized constraint: "VarName OP Value"
struct VarConstraint {
    std::string var_name;
    BinaryOp op;
    ExprAst value;
    ExprAst original_expr;
    size_t source_index = 0;

    bool is_double() const { return boost::get<double>(&value) != nullptr; }
    double get_double() const { return boost::get<double>(value); }

    bool is_stricter_than(const VarConstraint& other, BinaryOp chain_type) const {
        if (!is_double() || !other.is_double()) return false;
        double v1 = get_double();
        double v2 = other.get_double();

        if (chain_type == BinaryOp::And) {
            if (op == BinaryOp::Gt || op == BinaryOp::GtEq) return v1 > v2;
            if (op == BinaryOp::Lt || op == BinaryOp::LtEq) return v1 < v2;
        } else {
             if (op == BinaryOp::Gt || op == BinaryOp::GtEq) return v1 < v2;
             if (op == BinaryOp::Lt || op == BinaryOp::LtEq) return v1 > v2;
        }
        return false;
    }
};

class Simplifier : public boost::static_visitor<ExprAst>
{
public:
    static ExprAst simplify(const ExprAst& ast)
    {
        Simplifier visitor;
        return boost::apply_visitor(visitor, ast);
    }

    // --- Base Cases ---
    template <typename T>
    ExprAst operator()(const T& val) const
    {
        return val;
    }

    ExprAst operator()(const Unary& u) const
    {
        ExprAst simple_expr = boost::apply_visitor(*this, u.expr);

        // Double negation: !!A -> A
        if (u.op == UnaryOp::Not) {
            if (auto* child = boost::get<Unary>(&simple_expr)) {
                if (child->op == UnaryOp::Not)
                    return child->expr;
            }
        }
        return Unary(u.op, std::move(simple_expr));
    }

    ExprAst operator()(const FuncCall& f) const
    {
        FuncCall new_f = f;
        for (auto& arg : new_f.args)
            arg = boost::apply_visitor(*this, arg);
        return new_f;
    }

    ExprAst operator()(const Binary& b) const
    {
        // 1. First, simplify children recursively
        ExprAst left  = boost::apply_visitor(*this, b.left);
        ExprAst right = boost::apply_visitor(*this, b.right);

        // 2. Logic Chain Optimization (AND/OR)
        if (b.op == BinaryOp::And || b.op == BinaryOp::Or) {
            return simplify_logic_chain(b.op, std::move(left), std::move(right));
        }

        return Binary(b.op, std::move(left), std::move(right));
    }

private:
    // Try to parse "Var op Literal" or "Literal op Var"
    std::optional<VarConstraint> extract_constraint(const ExprAst& node) const
    {
        const Binary* b = boost::get<Binary>(&node);
        bool is_negated = false;

        // Peek inside UnaryOp::Not
        if (!b) {
            if (const Unary* u = boost::get<Unary>(&node)) {
                if (u->op == UnaryOp::Not) {
                    b = boost::get<Binary>(&u->expr);
                    is_negated = (b != nullptr);
                }
            }
        }

        if (!b)
            return std::nullopt;

        const VarRef* var = nullptr;
        const ExprAst* val_ast = nullptr;
        bool swapped = false;

        auto is_literal = [](const ExprAst& expr) {
            return boost::get<bool>(&expr) || boost::get<double>(&expr) || boost::get<std::string>(&expr);
        };

        if ((var = boost::get<VarRef>(&b->left)) && is_literal(b->right)) {
            swapped = false;
            val_ast = &b->right;
        } else if (is_literal(b->left) && (var = boost::get<VarRef>(&b->right))) {
            swapped = true;
            val_ast = &b->left;
        } else {
            return std::nullopt;
        }

        BinaryOp normalized_op = b->op;
        if (swapped) {
            switch (b->op) {
            case BinaryOp::Lt:   normalized_op = BinaryOp::Gt; break;
            case BinaryOp::LtEq: normalized_op = BinaryOp::GtEq; break;
            case BinaryOp::Gt:   normalized_op = BinaryOp::Lt; break;
            case BinaryOp::GtEq: normalized_op = BinaryOp::LtEq; break;
            case BinaryOp::Eq:
            case BinaryOp::NotEq: break;
            default: return std::nullopt;
            }
        }

        // Invert the operator if it was wrapped in a Not
        if (is_negated) {
            switch (normalized_op) {
            case BinaryOp::Eq:    normalized_op = BinaryOp::NotEq; break;
            case BinaryOp::NotEq: normalized_op = BinaryOp::Eq; break;
            case BinaryOp::Lt:    normalized_op = BinaryOp::GtEq; break;
            case BinaryOp::LtEq:  normalized_op = BinaryOp::Gt; break;
            case BinaryOp::Gt:    normalized_op = BinaryOp::LtEq; break;
            case BinaryOp::GtEq:  normalized_op = BinaryOp::Lt; break;
            default: return std::nullopt;
            }
        }

        if (normalized_op != BinaryOp::Lt
            && normalized_op != BinaryOp::LtEq
            && normalized_op != BinaryOp::Gt
            && normalized_op != BinaryOp::GtEq
            && normalized_op != BinaryOp::Eq
            && normalized_op != BinaryOp::NotEq)
        {
            return std::nullopt;
        }

        // Inequalities are strictly reserved for doubles
        if (normalized_op != BinaryOp::Eq && normalized_op != BinaryOp::NotEq) {
            if (boost::get<double>(val_ast) == nullptr) {
                return std::nullopt;
            }
        }

        // Note: 'node' here is still the original AST node (either Binary or Unary),
        // which perfectly preserves the user's original syntax!
        return VarConstraint{var->name, normalized_op, *val_ast, node};
    }

    // Merges redundant constraints (e.g., d >= 0.3 && d >= 0.4 -> d >= 0.4)
    void merge_var_constraints(std::vector<ExprAst>& terms, BinaryOp chain_op) const
    {
        struct Bounds
        {
            std::optional<VarConstraint> lower;
            std::optional<VarConstraint> upper;
            std::optional<VarConstraint> equal;
            std::vector<VarConstraint> not_equal;
        };

        std::map<std::string, Bounds> var_bounds;

        auto resolve_to_false = [&terms]()
        {
            terms.clear();
            terms.emplace_back(false);
        };

        auto is_same_value = [&](const ExprAst& v1, const ExprAst& v2) {
            const double* d1 = boost::get<double>(&v1);
            const double* d2 = boost::get<double>(&v2);
            if (d1 && d2) return std::abs(*d1 - *d2) < 1e-9;
            return equals_to(v1, v2);
        };

        for (size_t i = 0; i < terms.size(); ++i) {
            auto c = extract_constraint(terms[i]);
            if (!c)
                continue;

            c->source_index = i; // Remember the original position
            Bounds& b = var_bounds[c->var_name];

            if (chain_op == BinaryOp::And) {
                if (c->op == BinaryOp::Eq) {
                    if (b.equal && !is_same_value(b.equal->value, c->value)) {
                        return resolve_to_false();
                    }
                    b.equal = c;
                } else if (c->op == BinaryOp::NotEq) {
                    b.not_equal.push_back(*c);
                } else if (c->op == BinaryOp::Gt || c->op == BinaryOp::GtEq) {
                    if (!b.lower || c->is_stricter_than(*b.lower, chain_op)) {
                        b.lower = c;
                    }
                } else if (c->op == BinaryOp::Lt || c->op == BinaryOp::LtEq) {
                    if (!b.upper || c->is_stricter_than(*b.upper, chain_op)) {
                        b.upper = c;
                    }
                }
            }
        }

        if (chain_op != BinaryOp::And)
            return;

        // Mask to track which terms survive the merge
        std::vector<bool> keep(terms.size(), false);

        // 1. Non-constraints are always kept
        for (size_t i = 0; i < terms.size(); ++i) {
            if (!extract_constraint(terms[i]))
                keep[i] = true;
        }

        // 2. Mark winning constraints to be kept
        for (auto& bounds : var_bounds | std::views::values) {
            if (bounds.lower && bounds.upper && bounds.lower->get_double() > bounds.upper->get_double()) {
                return resolve_to_false();
            }

            if (bounds.equal) {
                for (const auto& neq : bounds.not_equal) {
                    if (is_same_value(bounds.equal->value, neq.value))
                        return resolve_to_false();
                }
                if (bounds.equal->is_double()) {
                    double eq_val = bounds.equal->get_double();
                    if (bounds.lower) {
                        if (bounds.lower->op == BinaryOp::Gt && eq_val <= bounds.lower->get_double()) return resolve_to_false();
                        if (bounds.lower->op == BinaryOp::GtEq && eq_val < bounds.lower->get_double()) return resolve_to_false();
                    }
                    if (bounds.upper) {
                        if (bounds.upper->op == BinaryOp::Lt && eq_val >= bounds.upper->get_double()) return resolve_to_false();
                        if (bounds.upper->op == BinaryOp::LtEq && eq_val > bounds.upper->get_double()) return resolve_to_false();
                    }
                }
                // Mark the single Equality constraint to be kept
                keep[bounds.equal->source_index] = true;
            } else {
                if (bounds.lower)
                    keep[bounds.lower->source_index] = true;
                if (bounds.upper)
                    keep[bounds.upper->source_index] = true;
                for (const auto& neq : bounds.not_equal) {
                    bool redundant = false;
                    if (neq.is_double()) {
                        if (bounds.lower && neq.get_double() < bounds.lower->get_double())
                            redundant = true;
                        if (bounds.upper && neq.get_double() > bounds.upper->get_double())
                            redundant = true;
                    }
                    if (!redundant) {
                        keep[neq.source_index] = true;
                    }
                }
            }
        }

        // 3. Rebuild the vector in-place, perfectly preserving relative order
        std::vector<ExprAst> new_terms;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (keep[i]) {
                new_terms.push_back(std::move(terms[i]));
            }
        }

        terms = std::move(new_terms);
    }

    // Flattens a tree of (A && (B && C)) into a vector {A, B, C}
    void collect_operands(const ExprAst& node, BinaryOp op, std::vector<ExprAst>& list) const
    {
        if (const Binary* b = boost::get<Binary>(&node)) {
            if (b->op == op) {
                collect_operands(b->left, op, list);
                collect_operands(b->right, op, list);
                return;
            }
        }
        list.push_back(node);
    }

    // The heavy lifting: Idempotency and Absorption
    ExprAst simplify_logic_chain(BinaryOp op, ExprAst left, ExprAst right) const
    {
        std::vector<ExprAst> terms;

        // 1. Flatten
        Binary temp_node(op, std::move(left), std::move(right));
        collect_operands(temp_node, op, terms);

        // 1.5 Short-circuit and filter boolean literals
        std::vector<ExprAst> filtered_terms;
        for (auto& term : terms) {
            if (const bool* b = boost::get<bool>(&term)) {
                if (op == BinaryOp::And) {
                    if (!*b) return false; // false && ... -> false
                    continue;              // true && x -> x (drop true)
                } else if (op == BinaryOp::Or) {
                    if (*b) return true;   // true || ... -> true
                    continue;              // false || x -> x (drop false)
                }
            }
            filtered_terms.push_back(std::move(term));
        }

        if (filtered_terms.empty()) {
            return (op == BinaryOp::And) ? true : false;
        }
        terms = std::move(filtered_terms);

        // 2. Remove Duplicates (Idempotency: A && B && A -> A && B)
        std::vector<ExprAst> unique_terms;
        for (auto& term : terms) {
            auto it = std::ranges::find_if(
                unique_terms,
                [&](const ExprAst& u) { return equals_to(term, u); }
            );
            if (it == unique_terms.end()) {
                unique_terms.push_back(std::move(term));
            }
        }
        terms = std::move(unique_terms);

        // 3. Remove "Absorbed" terms
        BinaryOp anti_op = (op == BinaryOp::And) ? BinaryOp::Or : BinaryOp::And;
        std::vector<bool> absorbed(terms.size(), false);

        for (size_t i = 0; i < terms.size(); ++i) {
            if (absorbed[i])
                continue;

            for (size_t j = 0; j < terms.size(); ++j) {
                if (i == j || absorbed[j])
                    continue;

                if (const Binary* bin_j = boost::get<Binary>(&terms[j])) {
                    if (bin_j->op == anti_op) {
                        if (equals_to(terms[i], bin_j->left) || equals_to(terms[i], bin_j->right)) {
                            absorbed[j] = true;
                        }
                    }
                }
            }
        }

        // Compact vector and run constraint propagation
        std::vector<ExprAst> clean_terms;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (!absorbed[i])
                clean_terms.push_back(std::move(terms[i]));
        }
        terms = std::move(clean_terms);

        merge_var_constraints(terms, op);

        // 4. Rebuild the tree
        if (terms.empty()) {
            return (op == BinaryOp::And) ? true : false;
        }

        // If reduced to a single false due to contradiction, return early
        if (terms.size() == 1 && boost::get<bool>(&terms[0]) != nullptr) {
            return terms[0];
        }

        ExprAst result = std::move(terms[0]);
        for (size_t i = 1; i < terms.size(); ++i) {
            result = Binary(op, std::move(result), std::move(terms[i]));
        }

        return result;
    }
};

} // namespace

ExprAst simplify(const ExprAst& expr)
{
    return Simplifier::simplify(expr);
}

} // namespace Slic3r::Biz::Expr