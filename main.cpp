#include <iostream>
#include <sstream>

#include <llvm/Support/CommandLine.h>

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Frontend/CompilerInstance.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

class CastCallBack : public MatchFinder::MatchCallback {
    Rewriter& rwr;
public:
    CastCallBack(Rewriter& rewriter) : rwr(rewriter) {};

    virtual void run(const MatchFinder::MatchResult &Result) {
        const auto *expression = Result.Nodes.getNodeAs<CStyleCastExpr>("cast");
        if (expression == nullptr) return;

        auto location = CharSourceRange::getCharRange(
            expression->getLParenLoc(),
            expression->getSubExprAsWritten()->getBeginLoc());

        auto sourceType = Lexer::getSourceText(
            CharSourceRange::getTokenRange(
            expression->getLParenLoc().getLocWithOffset(1),
            expression->getRParenLoc().getLocWithOffset(-1)),
            *Result.SourceManager, Result.Context->getLangOpts());

        auto resType = ("static_cast<" + sourceType + ">(").str();
        rwr.ReplaceText(location, resType);

        auto closeParLoc = Lexer::getLocForEndOfToken(
            expression->getSubExprAsWritten()->IgnoreImpCasts()->getEndLoc(),
            0, *Result.SourceManager,
            Result.Context->getLangOpts());

        rwr.InsertText(closeParLoc, ")");
    }
};

class MyASTConsumer : public ASTConsumer {
public:
    MyASTConsumer(Rewriter &rewriter) : callback_(rewriter) {
        matcher_.addMatcher(
                cStyleCastExpr(unless(isExpansionInSystemHeader())).bind("cast"), &callback_);
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        matcher_.matchAST(Context);
    }

private:
    CastCallBack callback_;
    MatchFinder matcher_;
};

class CStyleCheckerFrontendAction : public ASTFrontendAction {
public:
    CStyleCheckerFrontendAction() = default;
    void EndSourceFileAction() override {
        rewriter_.getEditBuffer(rewriter_.getSourceMgr().getMainFileID())
            .write(llvm::outs());
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef /* file */) override {
        rewriter_.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<MyASTConsumer>(rewriter_);
    }

private:
    Rewriter rewriter_;
};

static llvm::cl::OptionCategory CastMatcherCategory("cast-matcher options");

int main(int argc, const char **argv) {
    CommonOptionsParser OptionsParser(argc, argv, CastMatcherCategory);
    ClangTool Tool(OptionsParser.getCompilations(),
            OptionsParser.getSourcePathList());

    return Tool.run(newFrontendActionFactory<CStyleCheckerFrontendAction>().get());
}
