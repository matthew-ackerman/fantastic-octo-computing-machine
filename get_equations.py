import math

def analytic_step(theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy, dt):
    """
    Closed-form step for thrust that rotates during the step.
    Uses constant angular acceleration for attitude *kinematics*,
    and constant mid-step angular rate for thrust direction during integration.
    """

    # --- Angular kinematics (exact for constant ddtheta) ---
    dtheta_new = dtheta + ddtheta*dt
    theta_new  = theta  + dtheta*dt + 0.5*ddtheta*dt*dt

    # --- Mass update (linear burn), use average mass on the step ---
    m_new  = m - dm*dt
    if m_new <= 0:
        raise ValueError("Mass dropped to zero or below in analytic_step.")
    m_avg = 0.5*(m + m_new)

    # --- Effective constant spin rate for thrust direction over the step ---
    # Use midpoint angular velocity for 2nd-order accuracy
    omega_mid = dtheta + 0.5*ddtheta*dt

    # We'll integrate the rotating unit thrust direction u(t) = [cos(theta + omega_mid*t), sin(theta + omega_mid*t)]
    # exactly over t in [0, dt], then scale by f/m_avg to get Δv, and by (t-s) kernel to get position contribution.

    T   = dt
    th0 = theta
    w   = omega_mid
    phi = w*T            # total *linearized* turn during the step
    c0, s0 = math.cos(th0), math.sin(th0)
    cT, sT = math.cos(th0 + phi), math.sin(th0 + phi)

    # --- Helper: small-angle safe integrals ---
    # I = ∫_0^T u(t) dt  (vector)  → contributes to Δv
    # S = ∫_0^T (T - s) u(s) ds   → contributes to Δx,Δy beyond dx*dt
    eps = 1e-8
    if abs(w) > eps:
        # Exact formulas for constant angular rate
        # I = [ (sin(th0+phi) - sin(th0))/w ,  (-cos(th0+phi) + cos(th0))/w ]
        Ix = (sT - s0)/w
        Iy = (-cT + c0)/w

        # S_x = ∫ (T - s) cos(th0 + w s) ds = (-T*sin(th0))/w - (cos(th0+phi) - cos(th0))/w^2
        # S_y = ∫ (T - s) sin(th0 + w s) ds = ( T*cos(th0))/w - (sin(th0+phi) - sin(th0))/w^2
        Sx = (-T*s0)/w - (cT - c0)/(w*w)
        Sy = ( T*c0)/w - (sT - s0)/(w*w)
    else:
        # Small w: use Taylor limits (stable as w→0)
        # u(t) ≈ [c0 - w t s0 - (w t)^2/2 c0,  s0 + w t c0 - (w t)^2/2 s0]
        # I  = ∫ u dt ≈ [ c0 T - s0 w T^2/2 - c0 w^2 T^3/6 ,  s0 T + c0 w T^2/2 - s0 w^2 T^3/6 ]
        wT  = w*T
        wT2 = wT*wT
        Ix = c0*T - s0*(w*T*T)/2.0 - c0*(w*w*T*T*T)/6.0
        Iy = s0*T + c0*(w*T*T)/2.0 - s0*(w*w*T*T*T)/6.0

        # S = ∫ (T - s) u(s) ds ≈
        # [ (T^2/2)c0 + (T^3/6)(-w s0) + (T^4/24)(-w^2 c0),
        #   (T^2/2)s0 + (T^3/6)( w c0) + (T^4/24)(-w^2 s0) ]
        T2, T3, T4 = T*T, T*T*T, T*T*T*T
        Sx = 0.5*T2*c0 + ( -w*s0 )*(T3/6.0) + ( -w*w*c0 )*(T4/24.0)
        Sy = 0.5*T2*s0 + (  w*c0 )*(T3/6.0) + ( -w*w*s0 )*(T4/24.0)

    # --- Linear dynamics updates ---
    # Δv = (f/m_avg) * I
    ax_int_x = (f/m_avg) * Ix
    ax_int_y = (f/m_avg) * Iy
    dx_new = dx + ax_int_x
    dy_new = dy + ax_int_y

    # Position: x_new = x + dx*T + (f/m_avg) * Sx, similarly for y

    print(x, dx, T, f, Sx)
    x = x + dx*T + (f/m_avg)*Sx
    y = y + dy*T + (f/m_avg)*Sy
    print(x)

    return theta_new, dtheta_new, ddtheta, f, dm, m_new, x, y, dx_new, dy_new
    #return theta,     dtheta,     ddtheta, f, dm, m,     x, y, dx, dy

def simulate_step(theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy, dt):
    # update angular velocity and angle
    dtheta = dtheta + ddtheta * dt
    theta = theta + dtheta * dt

    # mass decreases from fuel consumption
    m = m - dm * dt
    if m <= 0:
        raise ValueError("Mass dropped to zero or below!")

    # thrust components in world coordinates
    fx = f * math.cos(theta)
    fy = f * math.sin(theta)

    # accelerations
    ax = fx / m
    ay = fy / m

    # update velocity
    dx = dx + ax * dt
    dy = dy + ay * dt

    # update position
    x = x + dx * dt
    y = y + dy * dt

    return theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy

# initial conditions
theta   = 0.0      # pointing along +x
dtheta  = 0.0
ddtheta = 0.0      # constant angular accel
f       = 5.0     # thrust
dm      = 0.01     # fuel burn rate
m       = 100.0
x, y    = 0.0, 0.0
dx, dy  = 0.0, 0.0

dt = 1e-2          # timestep
steps = 10000

for step in range(steps):
    theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy = simulate_step(
        theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy, dt
    )

print(f"stepwise: x={x:.3f}, dx={dx:.3f}, y={y:.3f}, dy={dy:.3f}, theta={theta:.3f}")

theta   = 0.0      # pointing along +x
dtheta  = 0.0
ddtheta = 0.0      # constant angular accel
f       = 5.0     # thrust
dm      = 0.01     # fuel burn rate
m       = 100.0
x, y    = 0.0, 0.0
dx, dy  = 0.0, 0.0

print(steps*dt)

theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy = analytic_step(
        theta, dtheta, ddtheta, f, dm, m, x, y, dx, dy, dt*steps
    )

print(f"analytic: x={x:.3f}, dx={dx:.3f}, y={y:.3f}, dy={dy:.3f}, theta={theta:.3f}")
