#include "sbmlsim/internal/util/MathUtil.h"
#include <cmath>
#include <sbmlsim/internal/util/ASTNodeUtil.h>
#include <boost/math/common_factor_rt.hpp>

const unsigned long long FACTORIAL_TABLE[] = { // size 20
    1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800, 39916800, 479001600, 6227020800,
    87178291200, 1307674368000, 20922789888000, 355687428096000, 6402373705728000, 121645100408832000
    // 2432902008176640000, 51090942171709440000, 1124000727777607680000, 25852016738884976640000,
    // 620448401733239439360000, 15511210043330985984000000, 403291461126605635584000000,
    // 10888869450418352160768000000, 304888344611713860501504000000, 8841761993739701954543616000000,
    // 265252859812191058636308480000000, 8222838654177922817725562880000000, 263130836933693530167218012160000000,
    // 8683317618811886495518194401280000000, 295232799039604140847618609643520000000,
    // 10333147966386144929666651337523200000000, 371993326789901217467999448150835200000000,
    // 13763753091226345046315979581580902400000000, 523022617466601111760007224100074291200000000
};

double MathUtil::factorial(unsigned long long n) {
  if (n <= 19) {
    return FACTORIAL_TABLE[n];
  }

  double ret = FACTORIAL_TABLE[19];
  for (unsigned long long i = 20; i <= n; i++) {
    ret *= (double) i;
  }
  return ret;
}

long long MathUtil::ceil(double f) {
  return ::ceil(f);
}

long long MathUtil::floor(double f) {
  return ::floor(f);
}

double MathUtil::pow(double x, double y) {
  return ::pow(x, y);
}

double MathUtil::exp(double x) {
  return ::exp(x);
}

double MathUtil::fabs(double x) {
  return ::fabs(x);
}

bool MathUtil::isLong(double f) {
  return MathUtil::floor(f) == f;
}

bool MathUtil::isRationalForm(const ASTNode *ast) {
  auto type = ast->getType();
  // we only support (a/b), (a / b), (a * b^(-c)), (a * (b/c)).
  switch (type) {
    case AST_RATIONAL: {
      return true;
    }
    case AST_DIVIDE: {
      if (ast->getLeftChild()->isInteger() && ast->getRightChild()->isInteger()) {
        return true;
      }
      break;
    }
    case AST_TIMES: {
      // restricted to binary tree only.
      if (ast->getNumChildren() == 2 && ast->getLeftChild()->isInteger()) {
        auto right = ast->getRightChild();
        auto rightType = right->getType();
        if (rightType == AST_POWER || rightType == AST_FUNCTION_POWER) {
          return MathUtil::isRationalForm(right);
        } else if (rightType == AST_RATIONAL) {
          return true;
        }
      }
      break;
    }
    case AST_POWER:
    case AST_FUNCTION_POWER: {
      if (ast->getLeftChild()->isInteger() && ast->getRightChild()->isInteger()) {
        auto order = ast->getRightChild()->getValue();
        if (order < 0) {
          return true;
        }
      }
      break;
    }
    default:
      break;
  }
  return false;
}

ASTNode* MathUtil::reduceFraction(const ASTNode *ast) {
  // we only support (a/b), (a / b), (a * b^(-c)), (a * (b/c)).
  if (!MathUtil::isRationalForm(ast)) {
    return ast->deepCopy();
  }
  long numerator, denominator;
  auto type = ast->getType();

  switch(type) {
    case AST_RATIONAL: {
      numerator = ast->getNumerator();
      denominator = ast->getDenominator();
      break;
    }
    case AST_DIVIDE: {
      numerator = (long) ast->getLeftChild()->getValue();
      denominator = (long) ast->getRightChild()->getValue();
      break;
    }
    case AST_TIMES: {
      auto right = ast->getRightChild();
      auto rightType = right->getType();
      if (rightType == AST_POWER || rightType == AST_FUNCTION_POWER) { // (a* b^(-c)) -> (a/(b^c))
        numerator = ast->getLeftChild()->getValue();
        auto rational = MathUtil::reduceFraction(right);
        denominator = rational->getDenominator();
      } else if (rightType == AST_RATIONAL) { // (a * (b/c)) => (a*b/c)
        numerator = ast->getLeftChild()->getValue() * right->getNumerator();
        denominator = right->getDenominator();
      }
      break;
    }
    case AST_POWER:
    case AST_FUNCTION_POWER: { // (b^(-c)) -> (1/(b^c))
      auto order = ast->getRightChild()->getValue() * -1;
      numerator = 1;
      denominator = pow(ast->getLeftChild()->getValue(), order);
      break;
    }
    default:
      return ast->deepCopy(); // never reach here.
  }

  long gcd = boost::math::gcd(numerator, denominator);
  numerator /= gcd;
  denominator /= gcd;
  auto postReduction = new ASTNode();
  if (denominator == 1) {  // reduced to a number
    postReduction->setValue(numerator);
  } else { // still rational number
    postReduction->setType(AST_RATIONAL);
    postReduction->setValue(numerator, denominator);
  }
  return postReduction;
}

ASTNode* MathUtil::differentiate(const ASTNode *ast, std::string target) {
  // We do not expect that *ast is a binary tree, so we will convert it at first.
  ASTNode *binaryTree = ASTNodeUtil::reduceToBinary(ast);
  ASTNode *differentiatedRoot = new ASTNode();

  if (containsTarget(binaryTree, target) == 0) {
    differentiatedRoot->setType(AST_INTEGER);
    differentiatedRoot->setValue(0);
    return differentiatedRoot;
  }

  switch (binaryTree->getType()) {
    case AST_PLUS:
      /* d{u+v}/dx = du/dx + dv/dx */
      differentiatedRoot->setType(AST_PLUS);
      differentiatedRoot->addChild(differentiate(binaryTree->getLeftChild(), target));
      differentiatedRoot->addChild(differentiate(binaryTree->getRightChild(), target));
      break;
    case AST_MINUS:
      /* d{u-v}/dx = du/dx - dv/dx */
      differentiatedRoot->setType(AST_MINUS);
      differentiatedRoot->addChild(differentiate(binaryTree->getLeftChild(), target));
      differentiatedRoot->addChild(differentiate(binaryTree->getRightChild(), target));
      break;
    case AST_TIMES: {
      /* d{u*v}/dx = u * dv/dx + v * du/dx */
      differentiatedRoot->setType(AST_PLUS);
      ASTNode *left = new ASTNode(AST_TIMES);
      left->addChild(differentiate(binaryTree->getLeftChild(), target));
      left->addChild(binaryTree->getRightChild()->deepCopy());
      differentiatedRoot->addChild(left);
      ASTNode *right = new ASTNode(AST_TIMES);
      right->addChild(binaryTree->getLeftChild()->deepCopy());
      right->addChild(differentiate(binaryTree->getRightChild(), target));
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_DIVIDE: {
      /* d{u/v}/dx = (v * du/dx - u * dv/dx) / v^2 */
      differentiatedRoot->setType(AST_DIVIDE);
      if (containsTarget(binaryTree->getRightChild(), target) == 0) {
        differentiatedRoot->addChild(differentiate(binaryTree->getLeftChild(), target));
        differentiatedRoot->addChild(binaryTree->getRightChild()->deepCopy());
        break;
      }
      ASTNode *left = new ASTNode(AST_MINUS);
      ASTNode *ll = new ASTNode(AST_TIMES);
      ASTNode *lr = new ASTNode(AST_TIMES);
      ll->addChild(differentiate(binaryTree->getLeftChild(), target));
      ll->addChild(binaryTree->getRightChild()->deepCopy());
      lr->addChild(binaryTree->getLeftChild()->deepCopy());
      lr->addChild(differentiate(binaryTree->getRightChild(), target));
      left->addChild(ll);
      left->addChild(lr);
      ASTNode *right = new ASTNode(AST_FUNCTION_POWER);
      right->addChild(binaryTree->getRightChild()->deepCopy());
      ASTNode *rr = new ASTNode();
      rr->setValue(2);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_POWER:
    case AST_POWER: {
      /* d{u^v}/dx = v * u^(v-1) * du/dx + u^v * ln(u) * dv/dx */
      differentiatedRoot->setType(AST_PLUS);
      // Left multiply
      ASTNode *left = new ASTNode(AST_TIMES);  // v * u^(v-1) * du/dx
      ASTNode *minus = new ASTNode(AST_MINUS); // (v-1)
      ASTNode *one = new ASTNode();
      one->setValue(1);
      minus->addChild(binaryTree->getRightChild()->deepCopy()); // add v
      minus->addChild(one);
      ASTNode *power = new ASTNode(AST_POWER); // u^(v-1)
      power->addChild(binaryTree->getLeftChild()->deepCopy()); // add u
      power->addChild(minus);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target); // du/dx
      left->addChild(binaryTree->getRightChild()->deepCopy()); // add v
      left->addChild(power);
      left->addChild(dudx);
      // Right multiply
      ASTNode *right = new ASTNode(AST_TIMES);
      ASTNode *rpower = new ASTNode(AST_POWER);    // u^v
      rpower->addChild(binaryTree->getLeftChild()->deepCopy());  // add u
      rpower->addChild(binaryTree->getRightChild()->deepCopy()); // add v
      ASTNode *ln = new ASTNode(AST_FUNCTION_LN);
      ln->addChild(binaryTree->getLeftChild()->deepCopy());  // add u
      ASTNode *dvdx = differentiate(binaryTree->getRightChild()->deepCopy(), target); // dv/dx
      right->addChild(rpower);
      right->addChild(ln);
      right->addChild(dvdx);
      // add left and right
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ROOT: {
      /* convert root(n, x) to x^(-1 * n) */
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *left = binaryTree->getRightChild()->deepCopy();
      ASTNode *right = new ASTNode(AST_DIVIDE);
      ASTNode *rl = new ASTNode();
      rl->setValue(1);
      ASTNode *rr = binaryTree->getLeftChild()->deepCopy();
      right->addChild(rl);
      right->addChild(rr);
      power->addChild(left);
      power->addChild(right);
      differentiatedRoot = differentiate(power, target);
      break;
    }
    case AST_FUNCTION_SIN: {
      /* d{sin(u)}/dx = du/dx * cos(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_COS);
      right->addChild(binaryTree->getLeftChild()->deepCopy());
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_COS: {
      /* d{cos(u)}/dx = -1 * du/dx * sin(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = new ASTNode(AST_TIMES);
      ASTNode *ll = new ASTNode();
      ll->setValue(-1);
      left->addChild(ll);
      left->addChild(differentiate(binaryTree->getLeftChild(), target));
      ASTNode *right = new ASTNode(AST_FUNCTION_SIN);
      right->addChild(binaryTree->getLeftChild()->deepCopy());
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_TAN: {
      /* d{tan(u)}/dx = du/dx * sec(u)^2 */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_POWER);
      ASTNode *rl = new ASTNode(AST_FUNCTION_SEC);
      ASTNode *tmpl = binaryTree->getLeftChild()->deepCopy();
      rl->addChild(tmpl);
      ASTNode *rr = new ASTNode();
      rr->setValue(2);
      right->addChild(rl);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_SEC: {
      /* d{sec(u)}/dx = du/dx * sec(u) * tan(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_TIMES);
      ASTNode *rl = new ASTNode(AST_FUNCTION_SEC);
      rl->addChild(binaryTree->getLeftChild()->deepCopy());
      ASTNode *rr = new ASTNode(AST_FUNCTION_TAN);
      rr->addChild(binaryTree->getLeftChild()->deepCopy());
      right->addChild(rl);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_COT: {
      /* d{cot(u)}/dx = -1 * du/dx * cosec(u)^2   */
      /*              = -1 * du/dx / sin(u)^2 */
      /*     cosec(u) = 1 / sin(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_DIVIDE);
      ASTNode *rl = new ASTNode();
      rl->setValue(-1);
      ASTNode *rr = new ASTNode(AST_POWER);
      ASTNode *sin = new ASTNode(AST_FUNCTION_SIN);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      ASTNode *u = binaryTree->getLeftChild()->deepCopy();
      sin->addChild(u);
      rr->addChild(sin);
      rr->addChild(two);
      right->addChild(rl);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_SINH: {
      /* d{sinh(u)}/dx = du/dx * cosh(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_COSH);
      right->addChild(binaryTree->getLeftChild()->deepCopy());
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_COSH: {
      /* d{cosh(u)}/dx = du/dx * sinh(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_SINH);
      right->addChild(binaryTree->getLeftChild()->deepCopy());
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_TANH: {
      /* d{tanh(u)}/dx = du/dx * sech(u)^2 */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_POWER);
      ASTNode *rl = new ASTNode(AST_FUNCTION_SECH);
      rl->addChild(binaryTree->getLeftChild());
      ASTNode *rr = new ASTNode();
      rr->setValue(2);
      right->addChild(rl);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_SECH: {
      /* d{sech(u)}/dx = - du/dx * sech(u) * tanh(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *minus = new ASTNode();
      minus->setValue(-1);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *sech = new ASTNode(AST_FUNCTION_SECH);
      sech->addChild(binaryTree->getLeftChild()->deepCopy());
      ASTNode *tanh = new ASTNode(AST_FUNCTION_TANH);
      tanh->addChild(binaryTree->getLeftChild()->deepCopy());
      differentiatedRoot->addChild(minus);
      differentiatedRoot->addChild(dudx);
      differentiatedRoot->addChild(sech);
      differentiatedRoot->addChild(tanh);
      break;
    }
    case AST_FUNCTION_COTH: {
      /* d{coth(u)}/dx = - du/dx * cosech(u)^2 */
      /*               = - du/dx / sinh(u)^2   */
      /*     cosech(u) = 1 / sinh(u)           */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_DIVIDE);
      ASTNode *rl = new ASTNode();
      rl->setValue(-1);
      ASTNode *rr = new ASTNode(AST_POWER);
      ASTNode *sin = new ASTNode(AST_FUNCTION_SINH);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      ASTNode *u = binaryTree->getLeftChild()->deepCopy();
      sin->addChild(u);
      rr->addChild(sin);
      rr->addChild(two);
      right->addChild(rl);
      right->addChild(rr);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCSIN: {
      /* d{asin(u)}/dx = du/dx / sqrt(1 - u^2) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(one);
      minus->addChild(power);
      right->addChild(square);
      right->addChild(minus);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCCOS: {
      /* d{acos(u)}/dx = - du/dx / sqrt(1 - u^2) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = new ASTNode(AST_TIMES);
      ASTNode *minusone = new ASTNode();
      minusone->setValue(-1);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(one);
      minus->addChild(power);
      right->addChild(square);
      right->addChild(minus);
      left->addChild(minusone);
      left->addChild(dudx);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCTAN: {
      /* d{atan(u)}/dx = du/dx / (1 + u^2) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_PLUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      right->addChild(one);
      right->addChild(power);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCSEC: {
      /* d{arcsec(u)}/dx = du/dx / (|u| * sqrt(u^2 - 1)) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_TIMES);
      ASTNode *abs = new ASTNode();
      abs->setType(AST_FUNCTION_ABS);
      ASTNode *root = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(power);
      minus->addChild(one);
      root->addChild(two);
      root->addChild(minus);
      abs->addChild(binaryTree->getLeftChild()->deepCopy());
      right->addChild(abs);
      right->addChild(root);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCCSC: {
      /* d{arccsc(u)}/dx = - du/dx / (|u| * sqrt(u^2 - 1)) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *minusone = new ASTNode();
      minusone->setValue(-1);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      divide->setType(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *times = new ASTNode(AST_TIMES);
      ASTNode *abs = new ASTNode(AST_FUNCTION_ABS);
      ASTNode *root = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(power);
      minus->addChild(one);
      root->addChild(two);
      root->addChild(minus);
      abs->addChild(binaryTree->getLeftChild()->deepCopy());
      times->addChild(abs);
      times->addChild(root);
      divide->addChild(dudx);
      divide->addChild(times);
      differentiatedRoot->addChild(minusone);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_ARCCOT: {
      /* d{arccot(u)}/dx = - du/dx / (1 + u^2) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *minusone = new ASTNode();
      minusone->setValue(-1);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *plus = new ASTNode(AST_PLUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      plus->addChild(one);
      plus->addChild(power);
      divide->addChild(dudx);
      divide->addChild(plus);
      differentiatedRoot->addChild(minusone);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_ARCSINH: {
      /* d{arcsinh(u)}/dx = du/dx / sqrt(1 + u^2) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *plus = new ASTNode(AST_PLUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      plus->addChild(one);
      plus->addChild(power);
      right->addChild(square);
      right->addChild(plus);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCCOSH: {
      /* d{arccosh(u)}/dx = du/dx / sqrt(u^2 - 1) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(power);
      minus->addChild(one);
      right->addChild(square);
      right->addChild(minus);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCTANH: {
      /* d{arctanh(u)}/dx = du/dx / (1 - u^2) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *left = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *right = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      right->addChild(one);
      right->addChild(power);
      differentiatedRoot->addChild(left);
      differentiatedRoot->addChild(right);
      break;
    }
    case AST_FUNCTION_ARCSECH: {
      /* d{arcsech(u)}/dx = - du/dx / (u * sqrt(1 - u^2)) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *minusone = new ASTNode();
      minusone->setValue(-1);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      divide->setType(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *times = new ASTNode(AST_TIMES);
      ASTNode *root = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(one);
      minus->addChild(power);
      root->addChild(two);
      root->addChild(minus);
      times->addChild(binaryTree->getLeftChild()->deepCopy());
      times->addChild(root);
      divide->addChild(dudx);
      divide->addChild(times);
      differentiatedRoot->addChild(minusone);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_ARCCSCH: {
      /* d{arccsch(u)}/dx = - du/dx / (u * sqrt(1 + u^2)) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *minusone = new ASTNode();
      minusone->setValue(-1);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      divide->setType(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *times = new ASTNode(AST_TIMES);
      ASTNode *root = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *plus = new ASTNode(AST_PLUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      plus->addChild(power);
      plus->addChild(one);
      root->addChild(two);
      root->addChild(plus);
      times->addChild(binaryTree->getLeftChild()->deepCopy());
      times->addChild(root);
      divide->addChild(dudx);
      divide->addChild(times);
      differentiatedRoot->addChild(minusone);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_ARCCOTH: {
      /* d{arccoth(u)}/dx =   du/dx / (1 - u^2) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *plusone = new ASTNode();
      plusone->setValue(1);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *minus = new ASTNode(AST_MINUS);
      ASTNode *one = new ASTNode();
      one->setValue(1);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      power->addChild(binaryTree->getLeftChild()->deepCopy());
      power->addChild(two);
      minus->addChild(one);
      minus->addChild(power);
      divide->addChild(dudx);
      divide->addChild(minus);
      differentiatedRoot->addChild(plusone);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_EXP: {
      /* d{exp(u)}/dx = du/dx * exp(u) */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *exp = binaryTree->deepCopy();
      differentiatedRoot->addChild(dudx);
      differentiatedRoot->addChild(exp);
      break;
    }
    case AST_FUNCTION_LN: {
      /* d{ln(u)}/dx = du/dx / u */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *u = binaryTree->getLeftChild()->deepCopy();
      differentiatedRoot->addChild(dudx);
      differentiatedRoot->addChild(u);
      break;
    }
    case AST_FUNCTION_LOG: {
      /* d{log_base(u)}/dx = du/dx / (u * ln(base)) */
      differentiatedRoot->setType(AST_DIVIDE);
      ASTNode *base = binaryTree->getLeftChild()->deepCopy();
      ASTNode *dudx = differentiate(binaryTree->getRightChild(), target);
      ASTNode *u = binaryTree->getRightChild()->deepCopy();
      ASTNode *times = new ASTNode(AST_TIMES);
      ASTNode *ln = new ASTNode(AST_FUNCTION_LN);
      ln->addChild(base);
      times->addChild(u);
      times->addChild(ln);
      differentiatedRoot->addChild(dudx);
      differentiatedRoot->addChild(times);
      break;
    }
    case AST_FUNCTION_PIECEWISE: {
      differentiatedRoot->setType(AST_FUNCTION_PIECEWISE);
      for (int i = 0; i < binaryTree->getNumChildren(); i++) {
        if ((i % 2) == 0) {
          ASTNode *equation = differentiate(binaryTree->getChild(i), target);
          differentiatedRoot->addChild(equation);
        } else {
          differentiatedRoot->addChild(binaryTree->getChild(i)->deepCopy());
        }
      }
      break;
    }
    /**
     * Following codes are no exact method. It uses some approximation.
     */
    case AST_FUNCTION_ABS: {
      /** Approximation **/
      /* d{abs(u)}/dx = du/dx * u / |u| */
      /* Note: can not be applied for some x where u(x) == 0 */
      differentiatedRoot->setType(AST_TIMES);
      ASTNode *dudx = differentiate(binaryTree->getLeftChild(), target);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      ASTNode *u = binaryTree->getLeftChild()->deepCopy();
      ASTNode *abs = new ASTNode(AST_FUNCTION_ABS);
      abs->addChild(binaryTree->getLeftChild()->deepCopy()); // |u|
      divide->addChild(u);
      divide->addChild(abs);
      differentiatedRoot->addChild(dudx);
      differentiatedRoot->addChild(divide);
      break;
    }
    case AST_FUNCTION_CEILING:
    case AST_FUNCTION_FLOOR: {
      /** Approximation **/
      /* d{ceil(u)}/dx = 0  */
      /* Note: approximation. can not be applied if u(x) == integer value */
      differentiatedRoot->setValue(0);
      break;
    }
    case AST_FUNCTION_FACTORIAL: {
      /** Approximation **/
      /* Stirling's approximation              */
      /*   n! == sqrt(2 * PI * n) * (n / E)^n  */
      ASTNode *stirling = new ASTNode(AST_TIMES);
      ASTNode *n1 = binaryTree->getLeftChild()->deepCopy();
      ASTNode *n2 = binaryTree->getLeftChild()->deepCopy();
      ASTNode *n3 = binaryTree->getLeftChild()->deepCopy();
      ASTNode *sqrt = new ASTNode(AST_FUNCTION_ROOT);
      ASTNode *square = new ASTNode();
      square->setValue(2);
      ASTNode *sqrt_times = new ASTNode(AST_TIMES);
      ASTNode *two = new ASTNode();
      two->setValue(2);
      ASTNode *pi = new ASTNode(AST_CONSTANT_PI);
      ASTNode *power = new ASTNode(AST_POWER);
      ASTNode *divide = new ASTNode(AST_DIVIDE);
      ASTNode *e = new ASTNode(AST_CONSTANT_E);
      // left
      sqrt_times->addChild(two);
      sqrt_times->addChild(pi);
      sqrt_times->addChild(n1);
      sqrt->addChild(square);
      sqrt->addChild(sqrt_times);
      // right
      divide->addChild(n2);
      divide->addChild(e);
      power->addChild(divide);
      power->addChild(n3);
      // concat
      stirling->addChild(sqrt);
      stirling->addChild(power);
      // differentiate stirling
      differentiatedRoot = differentiate(stirling, target);
      break;
    }
    /**
     * Approximation block end.
     */
    case AST_REAL:
    case AST_INTEGER:
    case AST_NAME_TIME:
      differentiatedRoot->setType(AST_INTEGER);
      differentiatedRoot->setValue(0);
      break;
    case AST_NAME:
      differentiatedRoot->setType(AST_INTEGER);
      if (binaryTree->getName() == target) {
        differentiatedRoot->setValue(1);
      } else {
        differentiatedRoot->setValue(0);
      }
      break;
    default:
      std::cout << binaryTree->getType() << " is unknown" << std::endl;
      exit(1);
  }
  ASTNode* rtn = ASTNodeUtil::reduceToBinary(differentiatedRoot);
  return rtn;
}

bool MathUtil::containsTarget(const ASTNode *ast, std::string target)
{
  bool found = false;
  if (ast->getLeftChild() != nullptr) {
    found |= containsTarget(ast->getLeftChild(), target);
  }
  if (ast->getRightChild() != nullptr) {
    found |= containsTarget(ast->getRightChild(), target);
  }
  if (ast->getType() == AST_NAME) {
    std::string name = ast->getName();
    if (name == target) {
      return true;
    }
  }
  return found;
}

ASTNode* MathUtil::simplify(const ASTNode *ast) {
  ASTNode *binaryTree = ASTNodeUtil::reduceToBinary(ast);
  auto type = binaryTree->getType();
  ASTNode *left, *right, *simplifiedRoot, *tmpl, *tmpr;

  if ((!binaryTree->isOperator())
    && (binaryTree->getType() != AST_FUNCTION_POWER)
    && (binaryTree->getType() != AST_POWER)
    && (binaryTree->getType() != AST_FUNCTION_LN)
    && (binaryTree->getType() != AST_FUNCTION_PIECEWISE)
    && (binaryTree->getType() != AST_FUNCTION_SIN)
    && (binaryTree->getType() != AST_FUNCTION_COS)
    && (binaryTree->getType() != AST_FUNCTION_TAN)
      ) {
    return binaryTree;
  }

  // simplify only even index for AST_FUNCTION_PIECEWISE
  // AST_FUNCTION_PIECEWISE is not binay tree.
  if (binaryTree->getType() == AST_FUNCTION_PIECEWISE) {
    simplifiedRoot = new ASTNode(AST_FUNCTION_PIECEWISE);
    for (auto i = 0; i < binaryTree->getNumChildren(); i++) {
      if (i % 2 == 0) {
        simplifiedRoot->addChild(simplify(binaryTree->getChild(i)));
      } else {
        simplifiedRoot->addChild(binaryTree->getChild(i)->deepCopy());
      }
    }
    return simplifiedRoot;
  }

  left  = simplify(binaryTree->getLeftChild());
  auto left_val = left->getValue();

  // AST_FUNCTION_{LN,SIN,COS,TAN} only has 1 argument
  if (binaryTree->getType() == AST_FUNCTION_LN) {
    if (binaryTree->getLeftChild()->getType() == AST_CONSTANT_E) {
      simplifiedRoot = new ASTNode();
      simplifiedRoot->setValue(1);
      return simplifiedRoot;
    } else {
      return binaryTree;
    }
  } else if (binaryTree->getType() == AST_FUNCTION_SIN) {
    if (binaryTree->getLeftChild()->isNumber()) {
      if (binaryTree->getLeftChild()->getValue() == 0) {
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setValue(0);
        return simplifiedRoot;
      }
    }
    return binaryTree;
  } else if (binaryTree->getType() == AST_FUNCTION_COS) {
    if (binaryTree->getLeftChild()->isNumber()) {
      if (binaryTree->getLeftChild()->getValue() == 0) {
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setValue(1);
        return simplifiedRoot;
      }
    }
    return binaryTree;
  } else if (binaryTree->getType() == AST_FUNCTION_TAN) {
    if (binaryTree->getLeftChild()->isNumber()) {
      if (binaryTree->getLeftChild()->getValue() == 0) {
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setValue(0);
        return simplifiedRoot;
      }
    }
    return binaryTree;
  }

  // take care on 2nd argument
  right = simplify(binaryTree->getRightChild());
  auto right_val = right->getValue();

  switch (type) {
    case AST_PLUS:
      if (left->isNumber()) {
        left_val = left->getValue();
        if (left_val == 0.0) {
          return right->deepCopy();
        } else if (right->isNumber()) {
          right_val   = right->getValue();
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(left_val+right_val);
          return simplifiedRoot;
        } else if (right->getType() != AST_PLUS){  // left is an integer, right is not AST_PLUS. (3 + x) => (x + 3)
          simplifiedRoot = new ASTNode(AST_PLUS);
          simplifiedRoot->addChild(right->deepCopy());
          simplifiedRoot->addChild(left->deepCopy());
          return simplifiedRoot;
        }
      }
      if (right->isNumber()) { // left is not an integer
        right_val = right->getValue();
        if (right_val == 0.0) {
          return left->deepCopy();
        }
      }
      // merge "2 + x + 3" to "x + 5"
      if (left->getType() == AST_PLUS) {
        if (left->getRightChild()->isNumber() && right->isNumber()) {
          simplifiedRoot = new ASTNode(AST_PLUS);
          tmpl = left->getLeftChild()->deepCopy();
          tmpr = new ASTNode(AST_PLUS);
          tmpr->addChild(right->deepCopy());
          tmpr->addChild(left->getRightChild()->deepCopy());
          simplifiedRoot->addChild(tmpl);
          simplifiedRoot->addChild(simplify(tmpr));
          return simplifiedRoot;
        }
      }
      if (right->getType() == AST_PLUS) {
        if (right->getRightChild()->isNumber() && left->isNumber()) {
          simplifiedRoot = new ASTNode(AST_PLUS);
          tmpl = right->getLeftChild()->deepCopy();
          tmpr = new ASTNode(AST_PLUS);
          tmpr->addChild(left->deepCopy());
          tmpr->addChild(right->getRightChild()->deepCopy());
          simplifiedRoot->addChild(tmpl);
          simplifiedRoot->addChild(simplify(tmpr));
          return simplifiedRoot;
        }
      }
      break;
    case AST_MINUS:
      if (right->isNumber()) {
        right_val = right->getValue();
        if (right_val == 0.0) {
          return left->deepCopy();
        } else if (left->isNumber()) {
          left_val   = left->getValue();
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(left_val-right_val);
          return simplifiedRoot;
        }
      }
      break;
    case AST_TIMES: {
      if (left->isNumber()) {
        left_val = left->getValue();
        if (left_val == 0.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(0);
          return simplifiedRoot;
        } else if (left_val == 1.0) {
          return right->deepCopy();
        } else if (right->isNumber()) {
          right_val = right->getValue();
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(left_val * right_val);
          return simplifiedRoot;
        }
      }
      if (right->isNumber()) { // left is not an integer
        right_val = right->getValue();
        if (right_val == 0.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(0);
          return simplifiedRoot;
        } else if (right_val == 1.0) {
          return left->deepCopy();
        } else if (left->getType() != AST_TIMES) {   // left is not AST_TIMES, right is an integer. (x * 2) => (2 * x)
          simplifiedRoot = new ASTNode(AST_TIMES);
          simplifiedRoot->addChild(right->deepCopy());
          simplifiedRoot->addChild(left->deepCopy());
          return simplifiedRoot;
        }
      }
      // merge "2 * x * 3" to "6 * x"
      if (left->getType() == AST_TIMES) {
        if (left->getLeftChild()->isNumber() && right->isNumber()) {
          simplifiedRoot = new ASTNode(AST_TIMES);
          tmpl = new ASTNode(AST_TIMES);
          tmpl->addChild(left->getLeftChild()->deepCopy());
          tmpl->addChild(right->deepCopy());
          tmpr = left->getRightChild()->deepCopy();
          simplifiedRoot->addChild(simplify(tmpl));
          simplifiedRoot->addChild(tmpr);
          return simplifiedRoot;
        }
      }
      if (right->getType() == AST_TIMES) {
        if (right->getLeftChild()->isNumber() && left->isNumber()) {
          simplifiedRoot = new ASTNode(AST_TIMES);
          tmpl = new ASTNode(AST_TIMES);
          tmpl->addChild(right->getLeftChild()->deepCopy());
          tmpl->addChild(left->deepCopy());
          tmpr = right->getRightChild()->deepCopy();
          simplifiedRoot->addChild(simplify(tmpl));
          simplifiedRoot->addChild(tmpr);
          return simplifiedRoot;
        }
      }
      break; // can't simplify
    }
    case AST_DIVIDE:
      if (left->isNumber()) {
        left_val = left->getValue();
        if (left_val == 0.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(0);
          return simplifiedRoot;
        } else if (right->isNumber()) {
          right_val = right->getValue();
          double left_intpart;
          double right_intpart;
          if (left_val >= right_val && right_val != 0
              && std::modf(left_val, &left_intpart) == 0.0
              && std::modf(right_val, &right_intpart) == 0.0) {
            if ((int)left_val % (int)right_val == 0) {
              simplifiedRoot = new ASTNode();
              simplifiedRoot->setValue(left_val / right_val);
              return simplifiedRoot;
            }
          }
        }
      }
      if (right->isNumber()) { // left is not an integer
        right_val = right->getValue();
        if (right_val == 1.0) {
          return left->deepCopy();
        }
      }
      break; // can't simplify
    case AST_POWER:
    case AST_FUNCTION_POWER:
      if (left->isNumber()) {
        left_val = left->getValue();
        if (left_val == 0.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(0);
          return simplifiedRoot;
        } else if (left_val == 1.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(1);
          return simplifiedRoot;
        }
      }
      if (right->isNumber()) {
        right_val = right->getValue();
        if (right_val == 0.0) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(1);
          return simplifiedRoot;
        } else if (right_val == 1.0) {
          return left->deepCopy();
        }
      }
      if (left->getType() == AST_FUNCTION_POWER || left->getType() == AST_POWER) {  // pow(pow(x, 2), 3) => pow(x, 2*3)
        simplifiedRoot = new ASTNode(AST_POWER);
        simplifiedRoot->addChild(left->getLeftChild()->deepCopy());
        tmpr = new ASTNode(AST_TIMES);
        tmpr->addChild(left->getRightChild()->deepCopy());
        tmpr->addChild(right->deepCopy());
        simplifiedRoot->addChild(tmpr);
        return simplify(simplifiedRoot);
      }
      if (type == AST_FUNCTION_POWER) { // convert pow(x, y) to x ^ y
        simplifiedRoot = new ASTNode(AST_POWER);
        simplifiedRoot->addChild(left->deepCopy());
        simplifiedRoot->addChild(right->deepCopy());
        return simplifiedRoot;
      }
      break; // can't simplify
    default:
      break;
  }
  simplifiedRoot = new ASTNode(type);
  simplifiedRoot->addChild(left->deepCopy());
  simplifiedRoot->addChild(right->deepCopy());
  return simplifiedRoot;
}

ASTNode* MathUtil::simplifyNew(const ASTNode *ast) {
  ASTNode *inputAST, *outputAST;
  outputAST = ast->deepCopy();
  do {
    inputAST = outputAST;
    outputAST = simplifyTwoPath(inputAST);
  } while (!MathUtil::isEqualTree(inputAST, outputAST));
  return outputAST;
}

ASTNode* MathUtil::simplifyTwoPath(const ASTNode *ast) {
  auto postRuleOne = MathUtil::simplifyRuleOne(ast);
  auto postRuleTwo = MathUtil::simplifyRuleTwo(postRuleOne);
  return postRuleTwo;
}

ASTNode* MathUtil::simplifyRuleOne(const ASTNode *ast) {
  ASTNode *left, *right, *simplifiedRoot;
  std::vector<ASTNode*> children;

  if ((!ast->isOperator())
      //&& (ast->getType() != AST_FUNCTION_POWER)
      //&& (ast->getType() != AST_POWER)
      //&& (ast->getType() != AST_FUNCTION_LN)
      && (ast->getType() != AST_FUNCTION_PIECEWISE)
      //&& (ast->getType() != AST_FUNCTION_SIN)
      //&& (ast->getType() != AST_FUNCTION_COS)
      //&& (ast->getType() != AST_FUNCTION_TAN)
      ) {
    return ast->deepCopy();
  }

  // simplify only even index for AST_FUNCTION_PIECEWISE
  // AST_FUNCTION_PIECEWISE is not binay tree.
  if (ast->getType() == AST_FUNCTION_PIECEWISE) {
    simplifiedRoot = new ASTNode(AST_FUNCTION_PIECEWISE);
    for (auto i = 0; i < ast->getNumChildren(); i++) {
      if (i % 2 == 0) {
        simplifiedRoot->addChild(simplifyRuleOne(ast->getChild(i)));
      } else {
        simplifiedRoot->addChild(ast->getChild(i)->deepCopy());
      }
    }
    return simplifiedRoot;
  }

  for (auto i = 0; i < ast->getNumChildren(); i++) {
    children.push_back(simplifyRuleOne(ast->getChild(i)));
  }
  left  = children[0];
  right = children[1];

  auto type = ast->getType();
  switch (type) {
    case AST_PLUS: {
      // a + (b + c) == (a + b) + c -> a + b + c
      simplifiedRoot = new ASTNode();
      simplifiedRoot->setType(AST_PLUS);
      for (auto i = 0; i < children.size(); i++) {
        ASTNode *child = children[i];
        if (child->getType() == AST_PLUS) {
          for (auto j = 0; j < child->getNumChildren(); j++) {
            simplifiedRoot->addChild(child->getChild(j)->deepCopy());
          }
        } else {
          simplifiedRoot->addChild(child->deepCopy());
        }
      }
      return simplifiedRoot;
    }
    case AST_TIMES: {
      // a * (b * c) == (a * b) * c -> a * b * c
      simplifiedRoot = new ASTNode();
      simplifiedRoot->setType(AST_TIMES);
      for (auto i = 0; i < children.size(); i++) {
        ASTNode *child = children[i];
        if (child->getType() == AST_TIMES) {
          for (auto j = 0; j < child->getNumChildren(); j++) {
            simplifiedRoot->addChild(child->getChild(j)->deepCopy());
          }
        } else {
          simplifiedRoot->addChild(child->deepCopy());
        }
      }
      return simplifiedRoot;
    }
    case AST_MINUS: {
      // AST_MINUS tree is always a binary tree.
      if (right->isNumber()) {
        // a - b -> a + (-1*b) (if b is a constant)
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setType(AST_PLUS);
        auto right_val = right->getValue();
        right_val *= -1;
        ASTNode *minusNode = new ASTNode();
        minusNode->setValue(right_val);
        simplifiedRoot->addChild(left->deepCopy());
        simplifiedRoot->addChild(minusNode);
      } else {
        // a - f(x) -> a + (-1*f(x)) (if f(x) is a term)
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setType(AST_PLUS);
        ASTNode *times = new ASTNode();
        times->setType(AST_TIMES);
        ASTNode *minusone = new ASTNode();
        minusone->setValue(-1);
        times->addChild(minusone);
        times->addChild(ast->getRightChild()->deepCopy());
        simplifiedRoot->addChild(ast->getLeftChild()->deepCopy());
        simplifiedRoot->addChild(times);
      }
      return simplifyRuleOne(simplifiedRoot);
    }
    case AST_DIVIDE: {
      // AST_DIVIDE tree is always a binary tree.
      // x^2 / y^2 -> x^2 * y^(-2)
      simplifiedRoot = new ASTNode();
      simplifiedRoot->setType(AST_TIMES);
      simplifiedRoot->addChild(left->deepCopy());
      ASTNode *power = new ASTNode();
      power->setType(AST_POWER);
      auto rightType = right->getType();
      if (rightType == AST_POWER || rightType == AST_FUNCTION_POWER) {
        if (right->getRightChild()->isNumber()) {
          auto degree = right->getRightChild()->getValue();
          degree *= -1;
          ASTNode *minusDegree = new ASTNode();
          minusDegree->setValue(degree);
          power->addChild(right->getLeftChild()->deepCopy());
          power->addChild(minusDegree);
        }
      } else {
        power->addChild(right->deepCopy());
        ASTNode *minusOne = new ASTNode();
        minusOne->setValue(-1);
        power->addChild(minusOne);
      }
      simplifiedRoot->addChild(power);
      return simplifiedRoot;
    }
    default:
      break;
  }
  // power
  // exponent, base e
  // natural logarithm
  // sine
  // cosine
  // tangent
  // hyperbolic sine
  // hyperbolic cosine
  // hyperbolic tangent
  // inverse
  simplifiedRoot = new ASTNode(type);
  simplifiedRoot->addChild(left);
  simplifiedRoot->addChild(right);
  return simplifiedRoot;
}

ASTNode* MathUtil::simplifyRuleTwo(const ASTNode *ast) {
  ASTNode *left, *right, *simplifiedRoot;
  std::vector<ASTNode*> children;

  if ((!ast->isOperator())
      && (!ast->isRational())
      && (ast->getType() != AST_REAL)
      && (ast->getType() != AST_FUNCTION_POWER)
      && (ast->getType() != AST_POWER)
      && (ast->getType() != AST_FUNCTION_LN)
      && (ast->getType() != AST_FUNCTION_PIECEWISE)
      && (ast->getType() != AST_FUNCTION_SIN)
      && (ast->getType() != AST_FUNCTION_COS)
      && (ast->getType() != AST_FUNCTION_TAN)
      ) {
    return ast->deepCopy();
  }

  // simplify number. If a number is just an integer, return as AST_INTEGER
  if (ast->getType() == AST_REAL && MathUtil::isLong(ast->getValue())) {
    long lval = (long)ast->getValue();
    simplifiedRoot = new ASTNode();
    simplifiedRoot->setValue(lval);
    return simplifiedRoot;
  }
  // simplify rational number.
  if (ast->isRational()) {
    return MathUtil::reduceFraction(ast);
  }

  // simplify only even index for AST_FUNCTION_PIECEWISE
  // AST_FUNCTION_PIECEWISE is not binay tree.
  if (ast->getType() == AST_FUNCTION_PIECEWISE) {
    simplifiedRoot = new ASTNode(AST_FUNCTION_PIECEWISE);
    for (auto i = 0; i < ast->getNumChildren(); i++) {
      if (i % 2 == 0) {
        simplifiedRoot->addChild(simplifyRuleTwo(ast->getChild(i)));
      } else {
        simplifiedRoot->addChild(ast->getChild(i)->deepCopy());
      }
    }
    return simplifiedRoot;
  }

  for (auto i = 0; i < ast->getNumChildren(); i++) {
    children.push_back(simplifyRuleTwo(ast->getChild(i)));
  }
  left  = children[0];
  right = children[1];
  auto type = ast->getType();

  switch (type) {
    case AST_PLUS: {
      auto count = 0;
      auto countTimes = 0;
      double sum = 0.0;
      for (auto i = 0; i < children.size(); i++) {
        ASTNode *child = children[i];
        if (child->isNumber()) { // XXX rational
          sum += child->getValue();
          count++;
        } else if (child->getType() == AST_TIMES && child->getLeftChild()->isNumber()) {
          countTimes++;
        }
      }
      if (countTimes == children.size()) { // (a*f(x) + b*f(x)) -> (a+b)*f(x)
        bool isAllSameTree = true;
        ASTNode *compTree = children[0]->getRightChild();
        for (auto i = 1; i < children.size(); i++) {
          ASTNode *rightChildOfTimes = children[i]->getRightChild();
          if (!MathUtil::isEqualTree(compTree, rightChildOfTimes)) {
            isAllSameTree = false;
          }
        }
        if (isAllSameTree) {
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setType(AST_TIMES);
          ASTNode *sumNode = new ASTNode();
          double leftSum = 0.0;
          for (auto i = 0; i < children.size(); i++) {
            leftSum += children[i]->getLeftChild()->getValue();
          }
          sumNode->setValue(leftSum);
          simplifiedRoot->addChild(sumNode);
          simplifiedRoot->addChild(compTree->deepCopy());
          return simplifyRuleTwo(simplifiedRoot);
        }
      }
      if (count == children.size()) { // (1 + 2 + 3) -> 6
        simplifiedRoot = new ASTNode();
        simplifiedRoot->setValue(sum);
        return simplifiedRoot;
      } else {  // (x + y + 1 + 2) -> x + y + 3
        simplifiedRoot = new ASTNode(AST_PLUS);
        for (auto i = 0; i < children.size(); i++) {
          ASTNode *child = children[i];
          if (!child->isNumber()) {
            simplifiedRoot->addChild(child->deepCopy());
          }
        }
        if (sum != 0.0) {
          ASTNode *sumNode = new ASTNode();
          sumNode->setValue(sum);
          simplifiedRoot->addChild(sumNode);
        }
      }
      return simplifiedRoot;
    }
    case AST_TIMES: {
      ASTNode *rationalNode = nullptr;
      ASTNode *doubleNode = nullptr;
      auto countRational = 0;
      auto countDouble = 0;
      double productDouble = 1.0;
      long productNumerator = 1;
      long productDenominator = 1;
      for (auto i = 0; i < children.size(); i++) {
        ASTNode *child = children[i];
        if (child->isNumber()) {
          if (child->isRational() || child->isInteger()) { // for integer and rational numbers
            long nval = child->getNumerator();
            long dval = 1;
            if (nval == 0) {  // x * 0 -> 0
              simplifiedRoot = new ASTNode();
              simplifiedRoot->setValue(0.0);
              return simplifiedRoot;
            }
            productNumerator *= nval;
            if (child->isRational()) {
              dval = child->getDenominator();
            }
            productDenominator *= dval;
            countRational++;
          } else { // for real number
            double val;
            val = child->getValue();
            if (val == 0.0) {  // x * 0 -> 0
              simplifiedRoot = new ASTNode();
              simplifiedRoot->setValue(0.0);
              return simplifiedRoot;
            }
            productDouble *= val;
            countDouble++;
          }
        }
      }
      if (countRational > 0) {
        auto tmpRational = new ASTNode();
        tmpRational->setValue(productNumerator, productDenominator);
        rationalNode = MathUtil::reduceFraction(tmpRational);
        // if rationalNode is just a node of "1", then ignore it.
        if (rationalNode->isInteger() && rationalNode->getValue() == 1) {
          rationalNode = nullptr;
        }
      }
      if (countDouble > 0 && productDouble != 1.0) {
        if (rationalNode != nullptr && rationalNode->isInteger()) { // (1.5 * 3) -> 4.5
          // integer * real number -> real number
          productDouble *= rationalNode->getValue();
          rationalNode = nullptr;  // we don't need rationalNode anymore.
        }
        doubleNode = new ASTNode();
        doubleNode->setValue(productDouble);
      }
      if (countRational == children.size()) {      // (1 * 2 * 3) -> 6
        if (rationalNode == nullptr) {
          rationalNode = new ASTNode();
          rationalNode->setValue(1);
        }
        simplifiedRoot = rationalNode;
      } else if (countDouble == children.size()) { // (1.5 * 2.0) -> 3.0
        if (doubleNode == nullptr) {
          doubleNode = new ASTNode();
          doubleNode->setValue(1);
        }
        simplifiedRoot = doubleNode;
      } else {                                     // (x * y * 1.5 * 2) -> 3.0 * x * y
        simplifiedRoot = new ASTNode(AST_TIMES);
        if (rationalNode != nullptr) {
          simplifiedRoot->addChild(rationalNode);
        }
        if (doubleNode != nullptr) {
          simplifiedRoot->addChild(doubleNode);
        }
        for (auto i = 0; i < children.size(); i++) {
          ASTNode *child = children[i];
          if (!child->isNumber()) {
            simplifiedRoot->addChild(child->deepCopy());
          }
        }
        if (simplifiedRoot->getNumChildren() == 0) { // simplified result is "1".
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(1);
        }
      }
      // to support "(a*f(x) + b*f(x)) -> (a+b)*f(x)", we had to convert this AST to binary tree.
      return ASTNodeUtil::reduceToBinary(simplifiedRoot);
    }
    case AST_POWER:
    case AST_FUNCTION_POWER: {
      // AST_POWER and AST_FUNCTION_POWER tree is always a binary tree.
      if (left->isNumber()) {
        auto left_val = left->getValue();
        if (left_val == 0.0) {          // (0^n) -> 0
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(0);
          return simplifiedRoot;
        } else if (left_val == 1.0) {   // (1^n) -> 1
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(1);
          return simplifiedRoot;
        } else if (right->isNumber()) { // (2 ^ 3) -> 8
          if (right->getValue() > 0) {  // (2 ^ 3)
            auto right_val = right->getValue();
            simplifiedRoot = new ASTNode();
            simplifiedRoot->setValue(pow(left_val, right_val));
            return simplifiedRoot;
          } else if (right->getValue() == -1 && MathUtil::isLong(left_val)){ // (2 ^ -1)
            simplifiedRoot = new ASTNode();
            simplifiedRoot->setType(AST_RATIONAL);
            simplifiedRoot->setValue((long)1, (long)left_val); // sets (numerator, denominator)
            return simplifiedRoot;
          }
        }
      }
      if (right->isNumber()) {
        auto right_val = right->getValue();
        if (right_val == 0.0) {         // (x ^ 0) -> 1
          simplifiedRoot = new ASTNode();
          simplifiedRoot->setValue(1);
          return simplifiedRoot;
        } else if (right_val == 1.0) {  // (x ^ 1) -> x
          return left->deepCopy();
        }
      }
      if (left->getType() == AST_FUNCTION_POWER || left->getType() == AST_POWER) {
        if (left->getLeftChild()->isNumber() && right->isNumber()) {
          // pow(pow(2, x), 3) => pow(8, x)
          auto base = left->getLeftChild()->getValue();
          auto exponent = right->getValue();
          auto val = pow(base, exponent);
          ASTNode *baseNode = new ASTNode();
          baseNode->setValue(val);
          simplifiedRoot = new ASTNode(AST_POWER);
          simplifiedRoot->addChild(baseNode);
          simplifiedRoot->addChild(left->getRightChild()->deepCopy());
          return simplifyRuleTwo(simplifiedRoot);
        } else {
          // pow(pow(x, 2), 3) => pow(x, 2*3)
          simplifiedRoot = new ASTNode(AST_POWER);
          simplifiedRoot->addChild(left->getLeftChild()->deepCopy());
          ASTNode *tmpr = new ASTNode(AST_TIMES);
          tmpr->addChild(left->getRightChild()->deepCopy());
          tmpr->addChild(right->deepCopy());
          simplifiedRoot->addChild(tmpr);
          return simplifyRuleTwo(simplifiedRoot);
        }
      }
      if (type == AST_FUNCTION_POWER) { // convert pow(x, y) to x ^ y
        simplifiedRoot = new ASTNode(AST_POWER);
        simplifiedRoot->addChild(left->deepCopy());
        simplifiedRoot->addChild(right->deepCopy());
        return simplifiedRoot;
      }
      break; // can't simplify
    }
    default:
      break;
  }
  simplifiedRoot = new ASTNode(type);
  simplifiedRoot->addChild(left->deepCopy());
  simplifiedRoot->addChild(right->deepCopy());
  return simplifiedRoot;
}

ASTNode* MathUtil::taylorSeries(const ASTNode *ast, std::string target, double point, int order) {
  /**
   * f(x) = \sum_{n=0}^{\infty}{ f^n(a)/(n!) * (x - a)^n }
   * where f^0 = f, (x - a)^0 = 1, 0! = 1.
   */
  ASTNode *taylorSeriesRoot = new ASTNode();
  taylorSeriesRoot->setType(AST_PLUS);
  ASTNode *term = ast->deepCopy();
  ASTNode *nodeA = new ASTNode();
  nodeA->setValue(point);
  // generate f^0(a)
  ASTNode *substitute = term->deepCopy();
  substitute->replaceArgument(target, nodeA);
  taylorSeriesRoot->addChild(substitute);
  for (int n = 1; n < order+1; n++) {
    // f'
    ASTNode *dfdx = simplify(differentiate(term, target));
    substitute = dfdx->deepCopy();
    // f'(a)
    substitute->replaceArgument(target, nodeA->deepCopy());
    // n!
    ASTNode *factorial = new ASTNode();
    factorial->setValue(MathUtil::factorial(n));
    // f'/n!
    ASTNode *divide = new ASTNode(AST_DIVIDE);
    divide->addChild(substitute);
    divide->addChild(factorial);
    // (x - a)
    ASTNode *minus = new ASTNode(AST_MINUS);
    ASTNode *nodeX = new ASTNode(AST_NAME);
    nodeX->setName(target.c_str());
    minus->addChild(nodeX);
    minus->addChild(nodeA->deepCopy());
    // (x - a)^n
    ASTNode *power = new ASTNode(AST_POWER);
    ASTNode *nodeN = new ASTNode();
    nodeN->setValue(n);
    power->addChild(minus);
    power->addChild(nodeN->deepCopy());
    // combine
    ASTNode *times = new ASTNode(AST_TIMES);
    times->addChild(divide);
    times->addChild(power);
    // add to sigma
    taylorSeriesRoot->addChild(times);
    // prepare for next iteration
    delete(term);
    term = dfdx->deepCopy();
  }
  return taylorSeriesRoot;
}

bool MathUtil::isEqualTree(const ASTNode *ast1, const ASTNode *ast2) {
  // handle two ASTNodes with nullptr as equal
  if (ast1 ==  ast2) {
    return true;
  }
  if (ast1 == nullptr || ast2 == nullptr) {
    return false;
  }
  ASTNode* root1 = ASTNodeUtil::reduceToBinary(ast1);
  ASTNode* root2 = ASTNodeUtil::reduceToBinary(ast2);
  return ( ASTNodeUtil::isEqual(ast1, ast2) &&
      ( (isEqualTree(root1->getLeftChild(), root2->getLeftChild()) && isEqualTree(root1->getRightChild(), root2->getRightChild())) ||
        (isEqualTree(root1->getLeftChild(), root2->getRightChild()) && isEqualTree(root1->getRightChild(), root2->getLeftChild())) )
  );
}
