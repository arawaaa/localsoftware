import {
  Links,
  Meta,
  Outlet,
  Scripts,
  ScrollRestoration,
} from "react-router";
import { AppBar, Button, Toolbar, Typography, Container, Box, CssBaseline, ThemeProvider, createTheme } from '@mui/material';
import { Link as RouterLink } from 'react-router';

const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#90caf9',
    },
    secondary: {
      main: '#f48fb1',
    },
    background: {
      default: '#0a1929',
      paper: '#132f4c',
    },
    text: {
      primary: '#fff',
      secondary: 'rgba(255, 255, 255, 0.7)',
    },
  },
  typography: {
    fontFamily: '"Roboto", "Helvetica", "Arial", sans-serif',
    h1: { fontWeight: 700 },
    h2: { fontWeight: 600 },
    h3: { fontWeight: 600 },
  },
  components: {
    MuiAppBar: {
      styleOverrides: {
        root: {
          backgroundColor: '#0a1929',
          backgroundImage: 'none',
        },
      },
    },
  },
});

export function Layout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <head>
        <meta charSet="utf-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <link rel="icon" type="image/jpeg" href="/Orcas.jpg" />
        <title>Arnav Rawat</title>
        <Meta />
        <Links />
      </head>
      <body>
        {children}
        <ScrollRestoration />
        <Scripts />
      </body>
    </html>
  );
}

export default function App() {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box sx={{ flexGrow: 1, m: 0, p: 0, width: '100%', minHeight: '100vh', display: 'flex', flexDirection: 'column' }}>
        <AppBar 
          position="fixed" 
          color="primary"
          elevation={0} 
          sx={{ 
            borderBottom: '1px solid rgba(255,255,255,0.1)' 
          }}
        >
          <Toolbar>
            <Typography 
              variant="h6" 
              component={RouterLink} 
              to="/" 
              sx={{ 
                flexGrow: 1, 
                fontWeight: 'bold', 
                textDecoration: 'none', 
                color: 'inherit' 
              }}
            >
              Arnav Rawat
            </Typography>
            <Button 
              color="inherit" 
              component={RouterLink} 
              to="/writing"
              sx={{ fontWeight: 'medium' }}
            >
              Writing
            </Button>
            <Button 
              color="inherit" 
              component={RouterLink} 
              to="/dashboard"
              sx={{ fontWeight: 'medium' }}
            >
              Dashboard
            </Button>
            <Button
              color="inherit"
              href="https://arnavrawat.xyz:8443"
              sx={{ fontWeight: 'medium' }}
            >
              Network Monitor
            </Button>
          </Toolbar>
        </AppBar>
        
        <Box component="main" sx={{ flexGrow: 1 }}>
          <Outlet />
        </Box>
        
        <Box component="footer" sx={{ bgcolor: 'primary.main', color: 'white', py: 6 }}>
          <Container maxWidth="lg">
            <Typography variant="body2" align="center">
              © 2026 Arnav Rawat. Built with C++20, io_uring, and React.
            </Typography>
          </Container>
        </Box>
      </Box>
    </ThemeProvider>
  );
}
