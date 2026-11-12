#include "Slic3r/Biz/Expr/Parser.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/spirit/repository/include/qi.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/phoenix.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace phx = boost::phoenix;

using namespace Slic3r::Domain::Expr;

BOOST_FUSION_ADAPT_STRUCT(Binary,
    (BinaryOp, op)
    (ExprAst, left)
    (ExprAst, right)
)

BOOST_FUSION_ADAPT_STRUCT(Unary,
    (UnaryOp, op)
    (ExprAst, expr)
)

BOOST_FUSION_ADAPT_STRUCT(FuncCall,
    (std::string, name)
    (std::vector<ExprAst>, args)
)

BOOST_FUSION_ADAPT_STRUCT(VarRef,
    (std::string, name)
)

namespace Slic3r::Biz::Expr {

namespace {
template<typename Iterator>
struct ExprParser : qi::grammar<Iterator, ExprAst(), ascii::space_type>
{
    ExprParser() : ExprParser::base_type(logical_or)
    {
        using qi::double_;
        using qi::char_;
        using qi::lexeme;
        using qi::lit;
        using qi::string;
        using ascii::alpha;
        using ascii::alnum;

        auto kw = boost::spirit::repository::qi::distinct(qi::copy(alnum | '_'));

        raw_identifier = (alpha | char_('_')) >> *(alnum | char_('_'));
        identifier =
            qi::as_string[
                lexeme[raw_identifier]
            ];

        bool_constant = lexeme[
              qi::string("true")  [ qi::_val = true ]
            | qi::string("false") [ qi::_val = false ]
        ];

        string_constant %= lexeme[qi::as_string[
            qi::omit[lit('"')] >>
            *(('\\' >> char_) | (char_ - '"')) >>
            qi::omit[lit('"')]
        ]];

        regex_constant %= lexeme[qi::as_string[
            qi::omit[lit('/')] >>
            qi::raw[
                *( (lit('\\') >> char_) | (char_ - '/') )
            ] >>
            qi::omit[lit('/')]
        ]];


        func_call = qi::as_string[lexeme[raw_identifier]] >> '(' >> -(logical_or % ',') >> ')';

        var_ref = qi::as_string[
            lexeme[
                raw_identifier
                >> *(
                    char_('.') >> raw_identifier
                )
            ]
        ];
        factor = double_
            | bool_constant
            | string_constant
            | regex_constant
            | func_call
            | var_ref
            | '(' >> logical_or >> ')'
            ;

        unary =
              ( (lit('+') >> unary) [ qi::_val = phx::construct<Unary>(UnaryOp::Plus, qi::_1) ]
              | (lit('-') >> unary) [ qi::_val = phx::construct<Unary>(UnaryOp::Minus, qi::_1) ]
              | ( (lit('!') | kw["not"]) >> unary) [ qi::_val = phx::construct<Unary>(UnaryOp::Not, qi::_1) ]
              )
            | factor [ qi::_val = qi::_1 ]
            ;


        multiplicative = unary [ qi::_val = qi::_1 ] >>
            *( (char_('*') >> unary) [ qi::_val = phx::construct<Binary>(BinaryOp::Multiply, qi::_val, qi::_2) ]
             | (char_('/') >> unary) [ qi::_val = phx::construct<Binary>(BinaryOp::Divide, qi::_val, qi::_2) ]
             );

        additive = multiplicative [ qi::_val = qi::_1 ] >>
            *( (char_('+') >> multiplicative) [ qi::_val = phx::construct<Binary>(BinaryOp::Add, qi::_val, qi::_2) ]
             | (char_('-') >> multiplicative) [ qi::_val = phx::construct<Binary>(BinaryOp::Subtract, qi::_val, qi::_2) ]
             );

        relational = additive [ qi::_val = qi::_1 ] >>
            *( (string("<=") >> additive) [ qi::_val = phx::construct<Binary>(BinaryOp::LtEq, qi::_val, qi::_2) ]
             | (string(">=") >> additive) [ qi::_val = phx::construct<Binary>(BinaryOp::GtEq, qi::_val, qi::_2) ]
             | (char_('<') >> additive) [ qi::_val = phx::construct<Binary>(BinaryOp::Lt, qi::_val, qi::_2) ]
             | (char_('>') >> additive) [ qi::_val = phx::construct<Binary>(BinaryOp::Gt, qi::_val, qi::_2) ]
             | (string("=~") >> regex_constant) [qi::_val = phx::construct<Binary>(
                    BinaryOp::RegExMatch, qi::_val, phx::construct<RegEx>(qi::_2)
                )]
            | (string("!~") >> regex_constant) [qi::_val =
                phx::construct<Unary>(
                    UnaryOp::Not,
                    phx::construct<Binary>(
                        BinaryOp::RegExMatch,
                        qi::_val,
                        phx::construct<RegEx>(qi::_2)
                    )
                )]
            );

        equality = relational [ qi::_val = qi::_1 ] >>
            *( (string("==") >> relational) [ qi::_val = phx::construct<Binary>(BinaryOp::Eq, qi::_val, qi::_2) ]
             | (string("!=") >> relational) [ qi::_val = phx::construct<Binary>(BinaryOp::NotEq, qi::_val, qi::_2) ]
             );

        logical_and = equality [ qi::_val = qi::_1 ] >>
            *(
                ((string("&&") | kw["and"]) >> equality)[ qi::_val = phx::construct<Binary>(BinaryOp::And, qi::_val, qi::_2) ]
             );

        logical_or = logical_and [ qi::_val = qi::_1 ] >>
            *(
                ((string("||") | kw["or"]) >> logical_and) [ qi::_val = phx::construct<Binary>(BinaryOp::Or, qi::_val, qi::_2) ]
             );
        factor.name("factor");
        unary.name("unary");
        multiplicative.name("multiplicative");
        additive.name("additive");
        relational.name("relational");
        equality.name("equality");
        logical_and.name("logical_and");
        logical_or.name("logical_or");
        bool_constant.name("bool_constant");
        string_constant.name("string_constant");
        func_call.name("func_call");
        var_ref.name("var_ref");
        regex_constant.name("regex_constant");

#if 0
        debug(factor);
        debug(unary);
        debug(multiplicative);
        debug(additive);
        debug(relational);
        debug(equality);
        debug(logical_and);
        debug(logical_or);
        debug(bool_constant);
        debug(string_constant);
        debug(func_call);
        debug(var_ref);
        debug(regex_constant);
#endif
    }

    qi::rule<Iterator, ExprAst(), ascii::space_type> logical_or, logical_and, equality, relational,
        additive, multiplicative, unary, factor;
    qi::rule<Iterator, FuncCall(), ascii::space_type> func_call;
    qi::rule<Iterator, VarRef(), ascii::space_type> var_ref;
    qi::rule<Iterator, bool(), ascii::space_type> bool_constant;
    qi::rule<Iterator, std::string(), ascii::space_type> string_constant, identifier;
    qi::rule<Iterator, RegEx(), ascii::space_type> regex_constant;
    qi::rule<Iterator, std::vector<char>()> raw_identifier;
};

struct SourceLocation
{
    size_t line{0};
    size_t column{0};
    std::string line_text;

    std::string to_description() const
    {
        std::ostringstream os;
        os << line_text << "\n";
        for (size_t i = 1; i < column; ++i)
            os << "~";
        os << "^~\n";
        return os.str();
    }

    static SourceLocation from_iterator(std::string_view source, const std::string_view::iterator& pos)
    {
        size_t line{1};
        size_t column{1};
        auto line_start = source.begin();
        for (auto it = source.begin(); it != pos && it != source.end(); ++it) {
            if (*it == '\n') {
                line++;
                column = 1;
                line_start = it + 1;
            }
            else if (*it != '\r') {
                column++;
            }
        }
        auto line_end = std::find(pos, source.end(), '\n');

        std::string line_text(line_start, line_end);
        column += std::count(line_text.begin(), line_text.end(), '\t') * (4 - 1);
        boost::replace_all(line_text, "\t", "    ");
        return {line, column, std::move(line_text)};
    }
};

} // namespace

ExprAst Parser::parse(std::string_view source)
{
    // Grammar construction is expensive; the object is stateless across parses
    // and qi::phrase_parse never modifies it, so a single static instance is safe
    // even under TBB parallelism (C++11 guarantees thread-safe static init).
    static ExprParser<std::string_view::const_iterator> parser;
    ExprAst expr;
    auto start = source.begin();
    bool success = qi::phrase_parse(start, source.end(), parser, ascii::space, expr);
    if (!success || start != source.end()) {
        SourceLocation loc = SourceLocation::from_iterator(source, start);
        throw ParseError(fmt::format("Failed to parse expression:\n{}", loc.to_description()));
    }
    return  expr;
}

}